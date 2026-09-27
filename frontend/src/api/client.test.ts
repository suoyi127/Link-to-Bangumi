import { afterEach, expect, it, vi } from 'vitest'
import { addMikanFeed, ApiError, getInbox, getScans, startImportScan, startSourceScan } from './client'

afterEach(() => { vi.unstubAllGlobals(); vi.useRealTimers() })

it('requests a bounded inbox page with its offset', async () => {
  const fetchMock = vi.fn().mockResolvedValue(new Response(JSON.stringify({ items: [], nextOffset: null, total: 101 }), { status: 200 }))
  vi.stubGlobal('fetch', fetchMock)
  expect((await getInbox(100, 100)).total).toBe(101)
  expect(fetchMock).toHaveBeenCalledWith('/api/inbox?limit=100&offset=100', expect.anything())
})

it('requests only the selected inbox origin', async () => {
  const fetchMock = vi.fn().mockResolvedValue(new Response(JSON.stringify({ items: [], nextOffset: null, total: 0 }), { status: 200 }))
  vi.stubGlobal('fetch', fetchMock)
  await getInbox(100, 0, undefined, 'qb_download')
  expect(fetchMock).toHaveBeenCalledWith('/api/inbox?limit=100&offset=0&origin=qb_download', expect.anything())
})

it('preserves stable error metadata without exposing backend message', async () => {
  vi.stubGlobal('fetch', vi.fn().mockResolvedValue(new Response(JSON.stringify({ error: { code: 'invalid_page', message: 'private path' }, requestId: 'r-7' }), { status: 400 })))
  await expect(getScans()).rejects.toMatchObject({ code: 'invalid_page', requestId: 'r-7', status: 400 })
  try { await getScans() } catch (error) { expect((error as Error).message).not.toContain('private path') }
})

it('shows a qB action errorCode instead of a generic HTTP error', async () => {
  vi.stubGlobal('fetch', vi.fn().mockResolvedValue(new Response(JSON.stringify({ success: false, errorCode: 'qb_api_unavailable' }), { status: 503 })))
  await expect(addMikanFeed('https://mikanani.me/RSS/Bangumi?bangumiId=4011&subgroupid=203')).rejects.toMatchObject({ code: 'qb_api_unavailable', status: 503 })
})

it('turns network and malformed JSON failures into safe errors', async () => {
  vi.stubGlobal('fetch', vi.fn().mockRejectedValue(new TypeError('secret hostname')))
  await expect(getScans()).rejects.toMatchObject({ code: 'network_error', status: 0 })
  vi.stubGlobal('fetch', vi.fn().mockResolvedValue(new Response('invalid', { status: 200 })))
  await expect(getScans()).rejects.toMatchObject({ code: 'invalid_response', status: 200 })
})

it('uses a body-free POST for source scans', async () => {
  const fetchMock = vi.fn().mockResolvedValue(new Response(JSON.stringify({ id: 2, source: 'qb', status: 'completed', discoveredCount: 1, processedCount: 1, errorCount: 0, errorSummary: '', requestId: 'r' }), { status: 201 }))
  vi.stubGlobal('fetch', fetchMock)
  await startSourceScan()
  expect(fetchMock).toHaveBeenCalledWith('/api/scans', expect.objectContaining({ method: 'POST' }))
  expect(fetchMock.mock.calls[0][1].headers).toMatchObject({ Accept: 'application/json' })
  expect(ApiError).toBeDefined()
})

it.each([['source', startSourceScan], ['import', startImportScan]])('aborts a stalled %s scan after 20 seconds and clears its timer', async (_kind, scan) => {
  vi.useFakeTimers()
  const fetchMock = vi.fn().mockImplementation((_path, init: RequestInit) => new Promise((_resolve, reject) => {
    init.signal?.addEventListener('abort', () => reject(new DOMException('aborted', 'AbortError')))
  }))
  vi.stubGlobal('fetch', fetchMock)
  const pending = scan()
  const rejected = expect(pending).rejects.toMatchObject({ code: 'request_aborted', status: 0 })
  await vi.advanceTimersByTimeAsync(20_000)
  await rejected
  expect(fetchMock.mock.calls[0][1].signal.aborted).toBe(true)
  expect(vi.getTimerCount()).toBe(0)
})

it('reports an abort while reading JSON as request_aborted', async () => {
  vi.stubGlobal('fetch', vi.fn().mockResolvedValue({ ok: true, status: 200, headers: new Headers(), json: () => Promise.reject(new DOMException('aborted', 'AbortError')) }))
  await expect(startSourceScan()).rejects.toMatchObject({ code: 'request_aborted', status: 0 })
})
