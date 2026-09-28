import type { Anime, AnimeDetail, AuditLog, BangumiConfig, BangumiConnectionTest, BangumiSearch, Execution, InboxOrigin, InboxPage, Media, MetadataSync, MikanFeeds, Page, Preferences, Preview, QbAction, QbConfig, QbConfigDraft, QbMikanRule, QbStatus, Scan, Settings } from './types'

export class ApiError extends Error {
  constructor(public code: string, public status: number, public requestId?: string) {
    super(`请求失败（${code}${requestId ? `，请求 ID：${requestId}` : ''}）`)
    this.name = 'ApiError'
  }
}

// 统一处理同源请求、API 错误对象和取消信号，页面组件只消费业务数据。
async function request<T>(path: string, init?: RequestInit): Promise<T> {
  let response: Response
  try { response = await fetch(path, { ...init, headers: { Accept: 'application/json', ...init?.headers } }) }
  catch (error) {
    if (error instanceof DOMException && error.name === 'AbortError') throw new ApiError('request_aborted', 0)
    throw new ApiError('network_error', 0)
  }
  let body: unknown
  try { body = await response.json() }
  catch (error) {
    if (error instanceof DOMException && error.name === 'AbortError') throw new ApiError('request_aborted', 0)
    throw new ApiError('invalid_response', response.status, response.headers.get('X-Request-Id') ?? undefined)
  }
  if (typeof body !== 'object' || body === null || Array.isArray(body)) throw new ApiError('invalid_response', response.status)
  const payload = body as Record<string, unknown>
  const requestId = typeof payload.requestId === 'string' ? payload.requestId : response.headers.get('X-Request-Id') ?? undefined
  if (!response.ok) {
    const error = payload.error
    const code = typeof error === 'object' && error !== null && 'code' in error && typeof error.code === 'string'
      ? error.code
      : typeof payload.errorCode === 'string' && payload.errorCode ? payload.errorCode : 'http_error'
    throw new ApiError(code, response.status, requestId)
  }
  return payload as T
}

const pageQuery = (limit: number, offset: number) => `?limit=${limit}&offset=${offset}`
const json = (body: object): RequestInit => ({ method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body) })
export const getScans = (limit = 10, offset = 0, signal?: AbortSignal) => request<Page<Scan>>(`/api/scans${pageQuery(limit, offset)}`, { signal })
async function startScan(path: string) {
  const controller = new AbortController()
  const timer = setTimeout(() => controller.abort(), 20_000)
  try { return await request<Scan>(path, { method: 'POST', signal: controller.signal }) }
  finally { clearTimeout(timer) }
}
export const startSourceScan = () => startScan('/api/scans')
export const startImportScan = () => startScan('/api/imports/scan')
export const getInbox = (limit = 100, offset = 0, signal?: AbortSignal, origin?: InboxOrigin) => request<InboxPage>(`/api/inbox${pageQuery(limit, offset)}${origin ? `&origin=${encodeURIComponent(origin)}` : ''}`, { signal })
export const correctMedia = (id: number, correction: Pick<Media, 'title' | 'season' | 'episodeNumber' | 'episodeType'>) => request<Media>(`/api/inbox/${id}/parse`, json(correction))
export const getAnime = (limit = 50, offset = 0, signal?: AbortSignal) => request<Page<Anime>>(`/api/anime${pageQuery(limit, offset)}`, { signal })
export const getAnimeDetail = (id: number, mediaLimit = 50, mediaOffset = 0) => request<AnimeDetail>(`/api/anime/${id}?mediaLimit=${mediaLimit}&mediaOffset=${mediaOffset}`)
export const searchBangumi = (query: string) => request<BangumiSearch>(`/api/bangumi/search?q=${encodeURIComponent(query)}`)
export const bindBangumi = (id: number, subjectId: number) => request<AnimeDetail>(`/api/anime/${id}/bangumi`, { ...json({ subjectId, confirmed: true }), method: 'PUT' })
export const refreshAnimeCover = (id: number) => request<{ coverUrl: string }>(`/api/anime/${id}/cover/refresh`, { method: 'POST' })
export const playMedia = (id: number) => request<{ mediaId: number; status: 'started' }>(`/api/media/${id}/play`, { method: 'POST' })
export const previewOrganization = (mediaFileId: number, operation: Preferences['preferredOperation']) => request<Preview>('/api/organize/preview', json({ mediaFileId, operation }))
export const executeOrganization = (planId: number, idempotencyKey: string, qbDownloadComplete: boolean) => request<Execution>('/api/organize/execute', json({ planId, idempotencyKey, confirmed: true, qbDownloadComplete }))
export const getSettings = () => request<Settings>('/api/settings')
export const putQbDownloadDirectory = (path: string) => request<Settings & { restartRequired: boolean }>('/api/settings/qb-download-directory', { ...json({ path }), method: 'PUT' })
export const getQbStatus = () => request<QbStatus>('/api/qb/status')
export const getQbConfig = () => request<QbConfig>('/api/qb/config')
export const putQbConfig = (draft: QbConfigDraft) => request<QbConfig>('/api/qb/config', { ...json(draft), method: 'PUT' })
export const testQbConfig = (draft: QbConfigDraft) => request<QbStatus>('/api/qb/config/test', json(draft))
export const deleteQbConfig = () => request<QbConfig>('/api/qb/config', { method: 'DELETE' })
export const getBangumiConfig = () => request<BangumiConfig>('/api/bangumi/config')
export const putBangumiConfig = (userAgent: string) => request<BangumiConfig>('/api/bangumi/config', { ...json({ userAgent }), method: 'PUT' })
export const testBangumiConfig = (userAgent: string) => request<BangumiConnectionTest>('/api/bangumi/config/test', json({ userAgent }))
export const deleteBangumiConfig = () => request<BangumiConfig>('/api/bangumi/config', { method: 'DELETE' })
export const getMikanFeeds = () => request<MikanFeeds>('/api/qb/rss')
export const addMikanFeed = (url: string) => request<QbAction>('/api/qb/rss/feeds', json({ url }))
export const createMikanRule = (rule: QbMikanRule) => request<QbAction>('/api/qb/rss/rules', json(rule))
export const syncMetadata = () => request<MetadataSync>('/api/enrichment/sync', { method: 'POST' })
export const putSettings = (preferences: Preferences) => request<Settings>('/api/settings', { ...json(preferences), method: 'PUT' })
export const getAuditLogs = (limit = 50, offset = 0, animeId?: number) => request<Page<AuditLog>>(`/api/audit-logs${pageQuery(limit, offset)}${animeId === undefined ? '' : `&animeId=${animeId}`}`)
