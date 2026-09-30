type DesktopBridge = { webview?: { postMessage: (message: unknown) => void } }

export function requestDesktopRestart(required: boolean): boolean {
  const bridge = (window as Window & { chrome?: DesktopBridge }).chrome?.webview
  if (!required || !bridge || typeof bridge.postMessage !== 'function') return false
  // 仅请求桌面宿主管理它自己的后端，普通浏览器不尝试结束进程。
  bridge.postMessage({ command: 'restart-backend' })
  return true
}
