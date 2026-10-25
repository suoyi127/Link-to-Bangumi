import { afterEach, expect, it, vi } from 'vitest'
import { addMikanFeed } from './qb'
import { ApiError } from './request'
import { getInbox, getScans, startImportScan, startSourceScan, scanFolderImport } from './media'
import { getAnimeCalendar } from './anime'

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

it('distinguishes local request pressure from remote Bangumi rate limiting', () => {
  expect(new ApiError('bangumi_remote_rate_limited', 429).message).toContain('Bangumi 服务端限流')
  expect(new ApiError('bangumi_queue_full', 503).message).toContain('本地请求队列已满')
  expect(new ApiError('bangumi_local_rate_limited', 429).message).toContain('本地请求额度')
})


it('prefers the body request ID and retains conflict target metadata', async () => {
  vi.stubGlobal('fetch', vi.fn().mockResolvedValue(new Response(JSON.stringify({ error: { code: 'anime_merge_required' }, requestId: 'body-id', targetAnimeId: 7, targetTitle: '本地番剧' }), { status: 409, headers: { 'X-Request-Id': 'header-id' } })))
  await expect(getScans()).rejects.toMatchObject({ code: 'anime_merge_required', requestId: 'body-id', targetAnimeId: 7, targetTitle: '本地番剧', status: 409 })
})

it('falls back to the response header for an object without a request ID', async () => {
  vi.stubGlobal('fetch', vi.fn().mockResolvedValue(new Response(JSON.stringify({}), { status: 500, headers: { 'X-Request-Id': 'header-id' } })))
  await expect(getScans()).rejects.toMatchObject({ code: 'http_error', requestId: 'header-id', status: 500 })
})

it.each([
  { label: 'null', body: null },
  { label: 'array', body: [] },
  { label: 'number', body: 3 },
  { label: 'string', body: 'invalid' },
  { label: 'boolean', body: true },
])('retains header request IDs when rejecting a non-object JSON response: $label', async ({ body }) => {
  vi.stubGlobal('fetch', vi.fn().mockResolvedValue(new Response(JSON.stringify(body), { status: 200, headers: { 'X-Request-Id': 'header-id' } })))
  await expect(getScans()).rejects.toMatchObject({ code: 'invalid_response', requestId: 'header-id', status: 200 })
})

it('preserves partial success data and business error codes on HTTP 200', async () => {
  const body = { days: [], fromCache: true, stale: true, updatedAt: 12, errorCode: 'bangumi_remote_rate_limited', requestId: 'partial-id' }
  vi.stubGlobal('fetch', vi.fn().mockResolvedValue(new Response(JSON.stringify(body))))
  await expect(getAnimeCalendar()).resolves.toEqual(body)
})

it('keeps a folder scan active for 120 seconds and clears its timer after abort', async () => {
  vi.useFakeTimers()
  const fetchMock = vi.fn().mockImplementation((_path, init: RequestInit) => new Promise((_resolve, reject) => {
    init.signal?.addEventListener('abort', () => reject(new DOMException('aborted', 'AbortError')))
  }))
  vi.stubGlobal('fetch', fetchMock)
  const rejected = expect(scanFolderImport(9)).rejects.toMatchObject({ code: 'request_aborted', status: 0 })
  await vi.advanceTimersByTimeAsync(119_999)
  expect(fetchMock.mock.calls[0][1].signal.aborted).toBe(false)
  await vi.advanceTimersByTimeAsync(1)
  await rejected
  expect(fetchMock.mock.calls[0][0]).toBe('/api/folder-imports/9/scan')
  expect(vi.getTimerCount()).toBe(0)
})

it('clears scan timers after success and HTTP failure', async () => {
  vi.useFakeTimers()
  vi.stubGlobal('fetch', vi.fn().mockResolvedValueOnce(new Response('{}')).mockResolvedValueOnce(new Response('{}', { status: 500 })))
  await startSourceScan()
  expect(vi.getTimerCount()).toBe(0)
  await expect(scanFolderImport(9)).rejects.toMatchObject({ code: 'http_error' })
  expect(vi.getTimerCount()).toBe(0)
})
