"""Browser interaction test against real ROS host fixture under /web_fixture."""
import asyncio
import contextlib
import json
import os
from pathlib import Path
import signal
import sys

from aiohttp import ClientSession
from playwright.async_api import async_playwright
from fixture_config import configure

TOOLS = Path(__file__).resolve().parents[3]


async def main():
    remappings = configure()
    port = int(os.environ.get('ROS_WEB_TEST_HTTP_PORT', '18766'))
    url = f'http://127.0.0.1:{port}'
    fixture = await asyncio.create_subprocess_exec('/usr/bin/python3', str(Path(__file__).with_name('ros_fixture.py')),
                                                   stdout=asyncio.subprocess.PIPE)
    marker = await asyncio.wait_for(fixture.stdout.readline(), 10)
    assert b'Domain 0' in marker, marker
    server = await asyncio.create_subprocess_exec(sys.executable, str(TOOLS / 'runtime/web/server.py'),
                                                   '--domain','0','--port',str(port), *remappings)
    errors, checks = [], []
    output = TOOLS / 'build/web'
    output.mkdir(parents=True, exist_ok=True)
    try:
        async with ClientSession() as session:
            for _ in range(100):
                try:
                    async with session.get(url + '/health') as response:
                        if response.status == 200:
                            break
                except OSError:
                    pass
                if server.returncode is not None:
                    raise RuntimeError('Server exited')
                await asyncio.sleep(0.1)
            else:
                raise TimeoutError('Server startup')
        async with async_playwright() as playwright:
            browser = await playwright.chromium.launch()
            page = await browser.new_page(viewport={'width':1440, 'height':1000})
            def record_error(error):
                errors.append(str(error))
                print('Browser error:', error, flush=True)
            page.on('pageerror', record_error)
            frames = []
            page.on('websocket', lambda ws: ws.on('framereceived', lambda data: frames.append(data) if len(frames) < 30 else None))
            await page.goto(url)
            await page.locator('#auto-agent').uncheck()
            await page.locator('#connect').click()
            await page.wait_for_function("document.getElementById('connection-state').textContent === '已连接'")
            try:
                await page.wait_for_function("document.getElementById('esp32s3-state').textContent === '在线'")
            except Exception:
                print('Initial WebSocket frames:', frames, flush=True)
                print('Browser log:', await page.locator('#log-view').text_content(), flush=True)
                raise
            await page.locator('#command-value').fill('57007')
            await page.locator('#publish-form button').click()
            await page.wait_for_function("document.getElementById('esp32s3-echo').textContent === '57007'")
            checks.append('Browser connection, heartbeats and real ROS Topic echo (host fixture)')
            await page.locator('#board').select_option('stm32')
            await page.locator('#operand-a').fill('9007199254740993')
            await page.locator('#operand-b').fill('1')
            await page.locator('#service-form button').click()
            await page.wait_for_function("document.getElementById('service-result').textContent.includes('9007199254740994')")
            await page.locator('#action-send').click()
            await page.wait_for_function("document.getElementById('action-result').textContent.includes('成功')")
            await page.locator('#order').fill('10')
            await page.locator('#action-send').click()
            await page.locator('#cancel').wait_for(state='visible')
            await page.wait_for_function("!document.getElementById('cancel').disabled")
            await page.locator('#cancel').click()
            await page.wait_for_function("document.getElementById('action-result').textContent.includes('已取消')")
            checks.append('Browser Service with Int64 precision, Action success and cancel')
            await page.screenshot(path=str(output / 'desktop.png'), full_page=True)
            await page.locator('[data-page="sensors"]').click()
            await page.locator('#sensor-refresh').click()
            await page.wait_for_function("Array.from(document.getElementById('sensor-topic').options).some(o=>o.text.includes('/web_fixture/temperature1'))")
            await page.locator('#sensor-topic').select_option(json.dumps({'topic':'/web_fixture/temperature1','type':'sensor_msgs/msg/Temperature'}, separators=(',',':')))
            await page.locator('#subscribe').click()
            await page.wait_for_function("document.getElementById('sensor-view').textContent.includes('25.5')")
            await page.locator('[data-page="graph"]').click()
            await page.locator('#graph-refresh').click()
            await page.wait_for_function("document.getElementById('nodes').textContent.includes('web_host_fixture')")
            await page.locator('[data-page="logs"]').click()
            assert '57007' in await page.locator('#log-view').text_content()
            checks.append('Sensor subscription, ROS graph and event log pages')
            await page.locator('[data-page="overview"]').click()
            await page.set_viewport_size({'width':390,'height':844})
            await page.screenshot(path=str(output / 'mobile.png'), full_page=True)
            assert await page.evaluate('document.documentElement.scrollWidth <= window.innerWidth')
            await page.locator('#connect').click()
            await page.wait_for_function("document.getElementById('connection-state').textContent === '未连接'")
            await page.locator('#connect').click()
            await page.wait_for_function("document.getElementById('stm32-state').textContent === '在线'")
            await page.reload()
            await page.locator('#auto-agent').uncheck()
            await page.locator('#connect').click()
            await page.wait_for_function("document.getElementById('connection-state').textContent === '已连接'")
            assert not errors, errors
            checks.append('Mobile layout, disconnect/reconnect, reload and no JavaScript errors')
            await browser.close()
        report = {'passed':True, 'fixture':'host-generated /web_fixture namespace in Domain 0; no physical MCU', 'checks':checks}
        (output / 'browser_report.json').write_text(json.dumps(report, indent=2) + '\n')
        print(json.dumps(report, indent=2))
    finally:
        for process, sig in ((server,signal.SIGINT),(fixture,signal.SIGTERM)):
            if process.returncode is None:
                process.send_signal(sig)
            with contextlib.suppress(asyncio.TimeoutError):
                await asyncio.wait_for(process.wait(), 6)
            if process.returncode is None:
                process.kill()
                await process.wait()


if __name__ == '__main__':
    asyncio.run(main())
