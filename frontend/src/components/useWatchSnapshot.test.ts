import { act, cleanup, renderHook } from '@testing-library/react'
import { afterEach, expect, it, vi } from 'vitest'
import { useWatchSnapshot } from './useWatchSnapshot'

afterEach(() => { cleanup(); vi.restoreAllMocks(); vi.useRealTimers() })

it('only polls a visible page and throttles repeated visibility events', async () => {
  vi.useFakeTimers()
  const hidden = vi.spyOn(document, 'hidden', 'get').mockReturnValue(true)
  const read = vi.fn().mockResolvedValue('loaded')
  renderHook(() => useWatchSnapshot(read))
  await act(() => vi.advanceTimersByTimeAsync(10000))
  expect(read).not.toHaveBeenCalled()
  hidden.mockReturnValue(false)
  await act(async () => {
    document.dispatchEvent(new Event('visibilitychange'))
    document.dispatchEvent(new Event('visibilitychange'))
  })
  expect(read).toHaveBeenCalledOnce()
  await act(() => vi.advanceTimersByTimeAsync(10000))
  expect(read).toHaveBeenCalledTimes(2)
})

it('ignores a stale poll after a successful local edit', async () => {
  let finish!: (value: string) => void
  const read = vi.fn(() => new Promise<string>(resolve => { finish = resolve }))
  const { result } = renderHook(() => useWatchSnapshot(read))
  act(() => result.current.update('已看'))
  await act(async () => finish('未看'))
  expect(result.current.data).toBe('已看')
})
