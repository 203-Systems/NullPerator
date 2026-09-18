// Installed in the trusted, offline entry document before the app module.
globalThis.__nullPeratorNativeCore = true;
globalThis.__nullPeratorNativePlatform = 'android';
(() => {
  let nextId = 0;
  const pending = new Map();
  globalThis.__nullPeratorAndroidReply = (id, result, error) => {
    const request = pending.get(id);
    if (!request) return;
    pending.delete(id);
    clearTimeout(request.timer);
    error ? request.reject(new Error(error)) : request.resolve(result);
  };
  globalThis.__nullPeratorNativeTransport = {
    postMessage(message) {
      return new Promise((resolve, reject) => {
        const id = ++nextId;
        const timer = setTimeout(() => { pending.delete(id); reject(new Error('Android bridge timed out')); }, 15000);
        pending.set(id, { resolve, reject, timer });
        NullPeratorAndroid.postMessage(JSON.stringify({ id, ...message }));
      });
    },
  };
})();
