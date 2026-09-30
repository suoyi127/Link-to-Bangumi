import { afterEach, expect, it, vi } from 'vitest'
import { requestDesktopRestart } from './desktop'

afterEach(() => vi.unstubAllGlobals())

it('does not request a restart in an ordinary browser', () => {
  expect(requestDesktopRestart(true)).toBe(false)
})

it('requests the desktop restart only after a directory requires it', () => {
  const postMessage = vi.fn()
  vi.stubGlobal('chrome', { webview: { postMessage } })
  expect(requestDesktopRestart(false)).toBe(false)
  expect(postMessage).not.toHaveBeenCalled()
  expect(requestDesktopRestart(true)).toBe(true)
  expect(postMessage).toHaveBeenCalledExactlyOnceWith({ command: 'restart-backend' })
})
