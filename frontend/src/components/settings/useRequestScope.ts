import { useEffect, useState } from 'react'

export const errorText = (error: unknown) => error instanceof Error ? error.message : '请求失败'

export function useRequestScope() {
  const [scope] = useState(() => {
    let generation = 0
    let active = true
    let controller: AbortController | undefined
    const invalidate = () => {
      generation++
      controller?.abort()
    }
    return {
      activate: () => { active = true },
      dispose: () => { active = false; invalidate() },
      invalidate,
      start: () => {
        invalidate()
        const current = generation
        controller = new AbortController()
        const signal = controller.signal
        return { signal, isCurrent: () => active && !signal.aborted && current === generation }
      },
    }
  })
  useEffect(() => {
    scope.activate()
    return scope.dispose
  }, [scope])
  return scope
}
