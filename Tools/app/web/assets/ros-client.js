// Reusable frontend SDK: no DOM, Qt, or ROS dependency.
export class RosClient extends EventTarget {
  constructor() { super(); this.socket = null; this.pending = new Map(); this.serial = 0; }
  connect(url) {
    if (this.socket) this.disconnect();
    const socket = new WebSocket(url);
    this.socket = socket;
    socket.addEventListener('message', ({data}) => {
      if (this.socket !== socket) return;
      let event;
      try { event = JSON.parse(data); } catch { return; }
      if (event.event === 'response') {
        const request = this.pending.get(event.id);
        if (request) {
          clearTimeout(request.timer); this.pending.delete(event.id);
          event.ok ? request.resolve(event.result) : request.reject(new Error(event.error));
        }
      }
      this.dispatchEvent(new CustomEvent('event', {detail:event}));
    });
    socket.addEventListener('close', () => {
      if (this.socket !== socket) return;
      this.socket = null; this.rejectPending('连接已断开');
      this.dispatchEvent(new Event('disconnected'));
    });
    socket.addEventListener('error', () => {
      if (this.socket === socket) this.dispatchEvent(new Event('connection-error'));
    });
  }
  request(op, values = {}) {
    if (!this.socket || this.socket.readyState !== WebSocket.OPEN) throw new Error('请先连接工作台');
    const id = String(++this.serial);
    const promise = new Promise((resolve, reject) => {
      const timer = setTimeout(() => { this.pending.delete(id); reject(new Error('请求超时')); }, 15000);
      this.pending.set(id, {resolve, reject, timer});
      try { this.socket.send(JSON.stringify({...values, id, op})); }
      catch (error) { clearTimeout(timer); this.pending.delete(id); reject(error); }
    });
    return {id, promise};
  }
  rejectPending(reason) {
    for (const {reject, timer} of this.pending.values()) { clearTimeout(timer); reject(new Error(reason)); }
    this.pending.clear();
  }
  disconnect() {
    const socket = this.socket; this.socket = null;
    socket?.close(); this.rejectPending('连接已断开');
    this.dispatchEvent(new Event('disconnected'));
  }
}
