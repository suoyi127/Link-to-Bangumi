import { useCallback, useEffect, useRef, useState } from 'react'

// 页面恢复可见时刷新；同一轮请求互斥，避免在播放器与窗口间切换时重复轮询。
export function useWatchSnapshot<T>(read: (signal: AbortSignal) => Promise<T>) {
  const [data, setData] = useState<T | null>(null)
  const [error, setError] = useState('')
  const revision = useRef(0)
  const refreshNow = useRef<(() => void) | null>(null)
  const update = useCallback((value: T | ((old: T | null) => T | null)) => {
    // 手动修改成功后，忽略修改前发出的查询。
    revision.current++
    setData(value)
    setError('')
  }, [])
  useEffect(() => {
    let active: AbortController | null = null
    let lastRead = 0
    setData(null)
    setError('')
    const readSnapshot = async (force: boolean) => {
      if (document.hidden || !force && (active || Date.now() - lastRead < 2000)) return
      // 标记结果可能影响同集的其它版本，需要立即用新查询替换在途轮询。
      active?.abort()
      const controller = new AbortController()
      active = controller
      lastRead = Date.now()
      const current = revision.current
      try {
        const value = await read(controller.signal)
        if (!controller.signal.aborted && current === revision.current) { setData(value); setError('') }
      } catch (cause) {
        if (!controller.signal.aborted && current === revision.current) setError(cause instanceof Error ? cause.message : '请求失败')
      } finally { if (active === controller) active = null }
    }
    const refresh = () => void readSnapshot(false)
    refreshNow.current = () => void readSnapshot(true)
    refresh()
    const timer = window.setInterval(refresh, 10000)
    document.addEventListener('visibilitychange', refresh)
    return () => { refreshNow.current = null; active?.abort(); clearInterval(timer); document.removeEventListener('visibilitychange', refresh) }
  }, [read])
  return { data, error, update, refresh: () => refreshNow.current?.() }
}

export function watchTime(seconds: number) {
  const value = Math.max(0, Math.floor(seconds))
  const minutes = Math.floor(value / 60)
  return `${String(minutes).padStart(2, '0')}:${String(value % 60).padStart(2, '0')}`
}
