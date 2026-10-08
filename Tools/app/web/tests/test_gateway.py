"""Real HTTP/WebSocket/ROS integration; isolated host fixture, no MCU required.

Run with ROS sourced and a Python environment containing aiohttp.
"""
import asyncio
import contextlib
import json
import os
from pathlib import Path
import sys

from aiohttp import ClientSession, WSServerHandshakeError, web
from fixture_config import configure

TOOLS = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(TOOLS / 'runtime/web'))
from server import create_app


async def receive(ws, predicate, timeout=10):
    async def find():
        while True:
            event = await ws.receive_json()
            if predicate(event):
                return event
    return await asyncio.wait_for(find(), timeout)


async def request(ws, ident, op, **kwargs):
    await ws.send_json({'id': ident, 'op': op, **kwargs})
    return await receive(ws, lambda event: event.get('event') == 'response' and event.get('id') == ident)


async def main():
    remappings = configure()
    fixture = await asyncio.create_subprocess_exec('/usr/bin/python3', str(Path(__file__).with_name('ros_fixture.py')),
                                                   stdout=asyncio.subprocess.PIPE)
    marker = await asyncio.wait_for(fixture.stdout.readline(), 10)
    assert b'Domain 0' in marker, marker
    app = create_app(['bash', str(TOOLS / 'runtime/ros2/run_worker.sh'), 'jazzy', '0', '1', *remappings])
    runner = web.AppRunner(app)
    checks = []
    try:
        await runner.setup()
        site = web.TCPSite(runner, '127.0.0.1', int(os.environ.get('ROS_WEB_TEST_HTTP_PORT', '18767')))
        await site.start()
        port = site._server.sockets[0].getsockname()[1]
        base = f'http://127.0.0.1:{port}'
        async with ClientSession() as session:
            async with session.get(base + '/') as response:
                assert response.status == 200 and 'MCU' in await response.text()
            async with session.get(base + '/assets/ros-client.js') as response:
                assert response.status == 200
            async with session.get(base + '/health') as response:
                assert (await response.json())['ready']
            try:
                await session.ws_connect(base + '/ws', origin='https://untrusted.example')
                raise AssertionError('Foreign origin accepted')
            except WSServerHandshakeError as error:
                assert error.status == 403
            checks.append('HTTP assets, health and foreign-origin rejection')
            async with session.ws_connect(base + '/ws', origin=base) as first, session.ws_connect(base + '/ws') as second:
                for ws in (first, second):
                    event = await receive(ws, lambda event: event.get('event') == 'ready')
                    assert event['domain'] == 0 and event['protocol'] == 1
                await first.send_str('not-json')
                assert not (await receive(first, lambda event: event.get('event') == 'response'))['ok']
                await first.send_json({'id':'same', 'op':'graph'})
                await second.send_json({'id':'same', 'op':'agent_status'})
                a = await receive(first, lambda event: event.get('id') == 'same')
                b = await receive(second, lambda event: event.get('id') == 'same')
                assert a['op'] == 'graph' and b['op'] == 'agent_status'
                assert not (await request(first, 'stop', 'stop'))['ok']
                assert not (await request(first, 'range', 'publish', board='esp32s3', value='2147483648'))['ok']
                checks.append('Malformed JSON recovery, client ID isolation and command validation')
                for _ in range(50):
                    graph = await request(first, 'discover', 'graph')
                    names = [topic['name'] for topic in graph['result']['topics']]
                    if '/web_fixture/temperature2' in names and '/web_fixture/stm32/fibonacci/_action/feedback' in names:
                        break
                    await asyncio.sleep(0.1)
                else:
                    raise AssertionError('Host fixture not discovered')
                await asyncio.sleep(0.4)
                for ws, i in ((first, 1), (second, 2)):
                    assert (await request(ws, 'sensor', 'subscribe', topic=f'/web_fixture/temperature{i}',
                                          type='sensor_msgs/msg/Temperature'))['ok']
                for ws, temperature in ((first, 25.5), (second, 26.5)):
                    for _ in range(3):
                        event = await receive(ws, lambda event: event.get('event') == 'sensor')
                        assert event['data']['temperature'] == temperature and '_client' not in event
                checks.append('Independent real sensor_msgs subscriptions in two clients')
                assert (await request(first, 'pub', 'publish', board='esp32s3', value='57007'))['ok']
                await receive(first, lambda event: event.get('event') == 'telemetry' and event.get('field') == 'echo' and event['value'] == '57007')
                result = await request(first, 'sum', 'service', board='stm32', a='9007199254740993', b='1')
                assert result['ok'] and result['result']['sum'] == '9007199254740994'
                checks.append('Real Topic echo and Service with lossless Int64 through JSON')
                await first.send_json({'id':'goal', 'op':'action', 'board':'stm32', 'order':10})
                await receive(first, lambda event: event.get('event') == 'action_accepted')
                feedback = await receive(first, lambda event: event.get('event') == 'action_feedback')
                assert feedback['sequence'][:2] == [0, 1]
                rejected = await request(second, 'foreign-cancel', 'cancel', board='stm32', goal_id='goal')
                assert not rejected['ok']
                await first.send_json({'id':'cancel', 'op':'cancel', 'board':'stm32', 'goal_id':'goal'})
                events = {}
                while len(events) < 2:
                    event = await receive(first, lambda event: event.get('event') == 'response' and event.get('id') in ('goal','cancel'))
                    events[event['id']] = event
                assert events['goal']['result']['status'] == 5 and events['cancel']['result']['cancel_requested']
                checks.append('Real Action feedback/cancel and cross-client cancel rejection')
                await first.send_json({'id':'disconnect-goal', 'op':'action', 'board':'stm32', 'order':100})
                await receive(first, lambda event: event.get('event') == 'action_accepted')
                await first.close()
                await asyncio.sleep(0.5)
                assert not any(route[1] == 'disconnect-goal' for route in app['gateway'].requests.values())
                result = await request(second, 'after-disconnect', 'action', board='stm32', order=6)
                assert result['ok'] and result['result']['sequence'] == [0,1,1,2,3,5]
                assert (await request(second, 'still-alive', 'graph'))['ok']
                checks.append('Client disconnect cancels its goal, releases board and keeps shared service alive')
        process = app['gateway'].process
        await runner.cleanup()
        assert process.returncode == 0
        checks.append('Service shutdown cleans up ROS child process')
        report = {'passed':True, 'fixture':'host-generated /web_fixture namespace in Domain 0; no physical MCU', 'checks':checks}
        output = TOOLS / 'build/web/gateway_report.json'
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(json.dumps(report, indent=2) + '\n')
        print(json.dumps(report, indent=2))
    finally:
        await runner.cleanup()
        if fixture.returncode is None:
            fixture.terminate()
        with contextlib.suppress(asyncio.TimeoutError):
            await asyncio.wait_for(fixture.wait(), 3)
        if fixture.returncode is None:
            fixture.kill()
            await fixture.wait()


if __name__ == '__main__':
    asyncio.run(main())
