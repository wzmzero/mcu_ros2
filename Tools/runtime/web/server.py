"""HTTP/WebSocket adapter for the ROS JSON-lines worker; no Qt or ROS imports."""
import argparse
import asyncio
import contextlib
import json
import logging
import os
from pathlib import Path
import uuid

from aiohttp import web, WSMsgType

LOG = logging.getLogger('ros_web')
TOOLS = Path(__file__).resolve().parents[2]
OPERATIONS = {'graph', 'publish', 'service', 'action', 'cancel', 'subscribe',
              'agent_start', 'agent_stop', 'agent_status'}


class Gateway:
    def __init__(self, worker_command):
        self.worker_command = worker_command
        self.process = None
        self.clients = {}
        self.requests = {}
        self.ready = None
        self.agent = None
        self.tasks = []
        self.write_lock = asyncio.Lock()
        self.closing = False

    async def start(self):
        self.process = await asyncio.create_subprocess_exec(
            *self.worker_command, stdin=asyncio.subprocess.PIPE,
            stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.PIPE,
            limit=2 * 1024 * 1024,
            env={**os.environ, 'ROS_WORKER_PYTHON': os.environ.get('ROS_WORKER_PYTHON', '/usr/bin/python3')})
        self.tasks = [asyncio.create_task(self.read_events()),
                      asyncio.create_task(self.read_logs())]
        # Fail startup concretely if ROS is missing instead of serving a dead UI.
        for _ in range(200):
            if self.ready:
                return
            if self.process.returncode is not None:
                raise RuntimeError('ROS worker exited during startup; see server log')
            await asyncio.sleep(0.1)
        raise RuntimeError('ROS worker startup timed out (20 seconds)')

    async def write(self, command):
        if self.process is None or self.process.returncode is not None:
            raise RuntimeError('ROS worker is unavailable; restart the service')
        async with self.write_lock:
            self.process.stdin.write((json.dumps(command, allow_nan=False) + '\n').encode())
            await asyncio.wait_for(self.process.stdin.drain(), 2)

    def offer(self, client, event):
        connection = self.clients.get(client)
        if connection:
            try:
                connection['queue'].put_nowait(event)
            except asyncio.QueueFull:
                # A slow browser cannot block DDS callbacks or other clients.
                if not connection['overflow']:
                    connection['overflow'] = True
                    asyncio.create_task(connection['ws'].close(code=1013, message=b'Client too slow'))

    def broadcast(self, event):
        for client in list(self.clients):
            self.offer(client, event)

    async def read_events(self):
        try:
            while line := await self.process.stdout.readline():
                try:
                    event = json.loads(line)
                    if not isinstance(event, dict):
                        raise ValueError('Expected event object')
                except ValueError:
                    message = line.decode(errors='replace').rstrip()[:2000]
                    LOG.warning('Worker output: %s', message)
                    self.broadcast({'event': 'log', 'message': message})
                    continue
                ident = event.get('id')
                if ident is not None:
                    route = self.requests.get(ident)
                    if route:
                        client, original = route
                        event['id'] = original
                        self.offer(client, event)
                        if event.get('event') == 'response':
                            self.requests.pop(ident, None)
                    continue
                client = event.pop('_client', None)
                if client is not None:
                    self.offer(client, event)
                    continue
                if event.get('event') == 'ready':
                    self.ready = {**event, 'protocol': 1}
                    event = self.ready
                elif event.get('event') == 'agent':
                    self.agent = event
                self.broadcast(event)
        except Exception:
            LOG.exception('ROS event reader failed')
        finally:
            self.ready = None
            if not self.closing:
                self.broadcast({'event': 'backend_error', 'error': 'ROS worker stopped; restart the service'})
                for ident, (client, original) in list(self.requests.items()):
                    self.offer(client, {'event': 'response', 'id': original, 'ok': False,
                                        'error': 'ROS worker stopped'})
                self.requests.clear()

    async def read_logs(self):
        while line := await self.process.stderr.readline():
            message = line.decode(errors='replace').rstrip()[:2000]
            LOG.warning('%s', message)
            self.broadcast({'event': 'log', 'message': message})

    async def accept_command(self, client, command):
        if not isinstance(command, dict):
            raise ValueError('Expected a JSON object')
        ident = command.get('id')
        if not isinstance(ident, str) or not 1 <= len(ident) <= 128:
            raise ValueError('id must be a string of 1–128 characters')
        if command.get('op') not in OPERATIONS:
            raise ValueError('Unknown operation')
        if not self.ready:
            raise RuntimeError('ROS worker is unavailable')
        internal = client + ':' + ident
        if internal in self.requests:
            raise ValueError('Request ID already active')
        if sum(route[0] == client for route in self.requests.values()) >= 64:
            raise ValueError('Too many pending requests')
        outgoing = {k: v for k, v in command.items() if not k.startswith('_')}
        outgoing.update(id=internal, _client=client)
        if command['op'] == 'cancel':
            goal = command.get('goal_id')
            if not isinstance(goal, str):
                raise ValueError('goal_id must be a string')
            outgoing['goal_id'] = client + ':' + goal
        self.requests[internal] = (client, ident)
        try:
            await self.write(outgoing)
        except Exception:
            self.requests.pop(internal, None)
            raise

    async def disconnect(self, client):
        self.clients.pop(client, None)
        for ident, route in list(self.requests.items()):
            if route[0] == client:
                self.requests.pop(ident, None)
        if self.ready and not self.closing:
            with contextlib.suppress(Exception):
                await self.write({'id': client + ':release', 'op': 'release_client', '_client': client})

    async def close(self):
        self.closing = True
        for connection in list(self.clients.values()):
            await connection['ws'].close(code=1001, message=b'Server shutting down')
        if self.process:
            if self.process.returncode is None:
                self.process.stdin.close()  # Worker EOF cancels goals and cleans up its owned Agent.
                try:
                    await asyncio.wait_for(self.process.wait(), 5)
                except asyncio.TimeoutError:
                    self.process.kill()
                    await self.process.wait()
        for task in self.tasks:
            task.cancel()
        await asyncio.gather(*self.tasks, return_exceptions=True)


def create_app(worker_command, frontend=TOOLS / 'app/web', allowed_origins=()):
    gateway = Gateway(worker_command)
    app = web.Application(client_max_size=65536)
    app['gateway'] = gateway

    async def lifecycle(app):
        try:
            await gateway.start()
            yield
        finally:
            await gateway.close()

    async def websocket(request):
        origin = request.headers.get('Origin')
        same_origin = f'{request.scheme}://{request.host}'
        if origin and origin != same_origin and origin not in allowed_origins:
            raise web.HTTPForbidden(text='WebSocket origin is not allowed')
        if len(gateway.clients) >= 16:
            raise web.HTTPServiceUnavailable(text='Client limit reached')
        ws = web.WebSocketResponse(heartbeat=20, max_msg_size=65536)
        await ws.prepare(request)
        client = uuid.uuid4().hex
        queue = asyncio.Queue(maxsize=256)
        gateway.clients[client] = {'ws': ws, 'queue': queue, 'overflow': False}

        async def sender():
            while True:
                await ws.send_json(await queue.get())

        task = asyncio.create_task(sender())
        if gateway.ready:
            gateway.offer(client, gateway.ready)
        else:
            gateway.offer(client, {'event': 'backend_error', 'error': 'ROS worker unavailable'})
        if gateway.agent:
            gateway.offer(client, gateway.agent)
        try:
            async for message in ws:
                if message.type == WSMsgType.TEXT:
                    command = None
                    try:
                        command = json.loads(message.data, parse_constant=lambda x: (_ for _ in ()).throw(ValueError('Non-finite JSON')))
                        await gateway.accept_command(client, command)
                    except Exception as exc:
                        gateway.offer(client, {'event': 'response',
                                              'id': command.get('id', '') if isinstance(command, dict) else '',
                                              'ok': False, 'error': str(exc)})
                elif message.type == WSMsgType.BINARY:
                    await ws.close(code=1003, message=b'JSON text required')
        finally:
            task.cancel()
            await asyncio.gather(task, return_exceptions=True)
            await gateway.disconnect(client)
        return ws

    async def health(request):
        return web.json_response({'ready': bool(gateway.ready), 'protocol': 1},
                                 status=200 if gateway.ready else 503)

    async def index(request):
        return web.FileResponse(frontend / 'index.html', headers={'Cache-Control': 'no-cache'})

    app.cleanup_ctx.append(lifecycle)
    app.router.add_get('/ws', websocket)
    app.router.add_get('/health', health)
    app.router.add_get('/', index)
    # Serve only frontend assets; never expose ROS scripts or repository files.
    app.router.add_static('/assets/', frontend / 'assets', show_index=False)
    return app


def main():
    parser = argparse.ArgumentParser(description='Independent ROS 2 WebSocket service and browser UI')
    parser.add_argument('--host', default='127.0.0.1')
    parser.add_argument('--port', type=int, default=8765)
    parser.add_argument('--distro', default='jazzy')
    parser.add_argument('--domain', type=int, default=0)
    parser.add_argument('--network-dds', action='store_true')
    parser.add_argument('--allow-origin', action='append', default=[], help='Exact additional frontend origin')
    parser.add_argument('--ros-args', nargs=argparse.REMAINDER, default=[], help='Pass ROS remapping arguments to worker')
    args = parser.parse_args()
    logging.basicConfig(level=logging.INFO)
    command = ['bash', str(TOOLS / 'runtime/ros2/run_worker.sh'), args.distro,
               str(args.domain), '0' if args.network_dds else '1']
    if args.ros_args:
        command += ['--ros-args', *args.ros_args]
    web.run_app(create_app(command, allowed_origins=args.allow_origin), host=args.host, port=args.port)


if __name__ == '__main__':
    main()
