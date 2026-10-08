"""Explicit two-MCU WebSocket test against a running service; never uses fixtures."""
import argparse
import asyncio
from collections import deque
import json
from pathlib import Path
import time

from aiohttp import ClientSession

TOOLS = Path(__file__).resolve().parents[3]


async def main(url):
    events = deque(maxlen=5000)
    responses = {}
    serial = 0
    checks = []

    async def wait(predicate, timeout=15):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            for event in list(events):
                if predicate(event):
                    return event
            await asyncio.sleep(0.01)
        raise TimeoutError('Required hardware response not received')

    async with ClientSession() as session, session.ws_connect(url) as ws:
        async def reader():
            async for message in ws:
                event = json.loads(message.data)
                events.append(event)
                if event.get('event') == 'response':
                    future = responses.get(event['id'])
                    if future and not future.done():
                        future.set_result(event)

        task = asyncio.create_task(reader())

        async def send(op, **kwargs):
            nonlocal serial
            serial += 1
            ident = str(serial)
            future = asyncio.get_running_loop().create_future()
            responses[ident] = future
            await ws.send_json({'id':ident, 'op':op, **kwargs})
            return ident, future

        async def request(op, **kwargs):
            ident, future = await send(op, **kwargs)
            try:
                result = await asyncio.wait_for(future,15)
                assert result['ok'], result
                return result['result']
            finally:
                responses.pop(ident, None)

        try:
            ready = await wait(lambda event: event.get('event') == 'ready')
            assert ready['domain'] == 0, 'Hardware validation expects configured MCU Domain 0'
            await request('agent_start', port=8888)
            agent = await wait(lambda event: event.get('event') == 'agent' and event['state'] == 'running')
            for board, command in (('esp32s3',57007),('stm32',57008)):
                deadline = time.monotonic() + 15
                while time.monotonic() < deadline:
                    values = {event['value'] for event in events if event.get('event') == 'telemetry'
                              and event.get('board') == board and event.get('field') == 'heartbeat'}
                    if len(values) >= 3:
                        break
                    await asyncio.sleep(0.05)
                else:
                    raise TimeoutError(f'{board}: missing three distinct real heartbeats')
                events.clear()
                await request('publish', board=board, value=str(command))
                await wait(lambda event: event.get('event') == 'telemetry' and event.get('board') == board
                           and event.get('field') == 'echo' and event['value'] == str(command))
                summed = await request('service',board=board,a='9007199254740993',b='1')
                assert summed['sum'] == '9007199254740994'
                result = await request('action',board=board,order=6)
                assert result['status'] == 4 and result['sequence'] == [0,1,1,2,3,5]
                rejected = await request('action',board=board,order=11)
                assert not rejected['accepted']
                ident, future = await send('action',board=board,order=10)
                await wait(lambda event: event.get('event') == 'action_accepted' and event.get('id') == ident)
                canceled = await request('cancel',board=board,goal_id=ident)
                result = await asyncio.wait_for(future,15)
                responses.pop(ident, None)
                assert canceled['cancel_requested'] and result['result']['status'] == 5
                checks.append(f'{board}: 3 real heartbeats, command/echo, Int64 Service, Action success/reject/cancel')
            report = {'passed':True,'fixture':False,'domain':0,'url':url,'agent_owned':agent['owned'],'checks':checks}
            output = TOOLS / 'build/web/hardware_report.json'
            output.parent.mkdir(parents=True,exist_ok=True)
            output.write_text(json.dumps(report,indent=2)+'\n')
            print(json.dumps(report,indent=2))
        finally:
            task.cancel()
            await asyncio.gather(task,return_exceptions=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--url',default='http://localhost:8765/ws')
    args = parser.parse_args()
    asyncio.run(main(args.url))
