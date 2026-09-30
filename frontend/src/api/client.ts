import type { Anime, AnimeDetail, AuditLog, BangumiConfig, BangumiConnectionTest, BangumiSearch, Execution, FolderImport, InboxOrigin, InboxPage, Media, MetadataSync, MikanFeeds, Page, Preferences, Preview, QbAction, QbConfig, QbConfigDraft, QbMikanRule, QbStatus, Scan, Settings } from './types'

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
export const getNovels = (signal?: AbortSignal) => request<{ items: import('./types').NovelWork[] }>('/api/novels', { signal })
export const getGames = (signal?: AbortSignal) => request<{ items: import('./types').GameResource[] }>('/api/games', { signal })
export const importGame = (path: string) => request<import('./types').GameScrape & { game: import('./types').GameResource }>('/api/game-imports', json({ path }))
export const editGame = (id: number, metadata: Pick<import('./types').GameResource, 'title' | 'developer' | 'summary' | 'path'>) => request<import('./types').GameResource>(`/api/games/${id}`, { ...json({ title: metadata.title, developer: metadata.developer, summary: metadata.summary, path: metadata.path }), method: 'PUT' })
export const removeUnavailableGame = (id: number) => request<{ removedId: number }>(`/api/games/${id}`, { ...json({ confirmed: true }), method: 'DELETE' })
export const launchGame = (id: number) => request<{ started: boolean }>(`/api/games/${id}/launch`, json({ confirmed: true }))
export const searchGameBangumi = (query: string) => request<{ items: import('./types').GameCandidate[] }>(`/api/game-bangumi/search?q=${encodeURIComponent(query)}`)
export const scrapeGame = (id: number, subjectId?: number) => request<import('./types').GameScrape>(`/api/games/${id}/scrape`, json(subjectId === undefined ? {} : { subjectId }))
export const searchGameVndb = (query: string) => request<{ items: import('./types').VndbCandidate[]; more: boolean }>(`/api/game-vndb/search?q=${encodeURIComponent(query)}`)
export const scrapeGameVndb = (id: number, vndbId?: string) => request<import('./types').GameScrape>(`/api/games/${id}/vndb/scrape`, json(vndbId === undefined ? {} : { vndbId }))
export const getNovelSources = () => request<{ items: import('./types').NovelSource[] }>('/api/novel-sources')
export const importNovels = (path: string) => request<{ fileCount: number; sourceId: number; scraping: boolean }>('/api/novel-imports', json({ path }))
export const getNovelReader = () => request<import('./types').NovelReader>('/api/novel-reader')
// 仅提交配置字段，避免把 GET 响应中的 requestId 回传给严格校验接口。
export const saveNovelReader = (config: import('./types').NovelReader) => request<import('./types').NovelReader>('/api/novel-reader', { ...json({ type: config.type, executable: config.executable }), method: 'PUT' })
export const editNovel = (id: number, metadata: Pick<import('./types').NovelWork, 'title' | 'author' | 'summary'>) => request<import('./types').NovelWork>(`/api/novels/${id}`, { ...json(metadata), method: 'PUT' })
export const editNovelFile = (id: number, workId: number, label: string) => request<import('./types').NovelWork>(`/api/novel-files/${id}`, { ...json({ workId, label }), method: 'PUT' })
export const removeUnavailableNovel = (id: number) => request<{ removedId: number }>(`/api/novels/${id}`, { ...json({ confirmed: true }), method: 'DELETE' })
export const readNovel = (id: number) => request<{ started: boolean }>(`/api/novel-files/${id}/read`, { method: 'POST' })
export const searchNovelBangumi = (query: string) => request<{ items: import('./types').NovelCandidate[] }>(`/api/novel-bangumi/search?q=${encodeURIComponent(query)}`)
export const scrapeNovel = (id: number, file = false, subjectId?: number) => request<import('./types').NovelScrape>(`/api/${file ? 'novel-files' : 'novels'}/${id}/scrape`, json(subjectId ? { subjectId } : {}))
export const getScans = (limit = 10, offset = 0, signal?: AbortSignal) => request<Page<Scan>>(`/api/scans${pageQuery(limit, offset)}`, { signal })
async function startScan(path: string, timeoutMs = 20_000) {
  const controller = new AbortController()
  const timer = setTimeout(() => controller.abort(), timeoutMs)
  try { return await request<Scan>(path, { method: 'POST', signal: controller.signal }) }
  finally { clearTimeout(timer) }
}
export const startSourceScan = () => startScan('/api/scans')
export const startImportScan = () => startScan('/api/imports/scan')
export const getFolderImports = () => request<{ items: FolderImport[] }>('/api/folder-imports')
export const addFolderImport = (path: string) => request<FolderImport>('/api/folder-imports', json({ path }))
export const scanFolderImport = (id: number) => startScan(`/api/folder-imports/${id}/scan`, 120_000)
export const getInbox = (limit = 100, offset = 0, signal?: AbortSignal, origin?: InboxOrigin) => request<InboxPage>(`/api/inbox${pageQuery(limit, offset)}${origin ? `&origin=${encodeURIComponent(origin)}` : ''}`, { signal })
export const correctMedia = (id: number, correction: Pick<Media, 'title' | 'season' | 'episodeNumber' | 'episodeType'>) => request<Media>(`/api/inbox/${id}/parse`, json(correction))
export const getAnime = (limit = 50, offset = 0, signal?: AbortSignal) => request<Page<Anime>>(`/api/anime${pageQuery(limit, offset)}`, { signal })
export const getAnimeCalendar = (signal?: AbortSignal) => request<import('./types').AnimeCalendar>('/api/anime-calendar', { signal })
export const getAnimeDetail = (id: number, mediaLimit = 50, mediaOffset = 0) => request<AnimeDetail>(`/api/anime/${id}?mediaLimit=${mediaLimit}&mediaOffset=${mediaOffset}`)
export const searchBangumi = (query: string) => request<BangumiSearch>(`/api/bangumi/search?q=${encodeURIComponent(query)}`)
export const bindBangumi = (id: number, subjectId: number) => request<AnimeDetail>(`/api/anime/${id}/bangumi`, { ...json({ subjectId, confirmed: true }), method: 'PUT' })
export const refreshAnimeCover = (id: number) => request<{ coverUrl: string }>(`/api/anime/${id}/cover/refresh`, { method: 'POST' })
export const getPlayers = (signal?: AbortSignal) => request<import('./types').PlayerCatalog>('/api/players', { signal })
export const savePlayer = (playerId: string, executable: string) => request<import('./types').PlayerCatalog>('/api/players', { method: 'PUT', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ playerId, executable }) })
export const playMedia = (id: number, playerId?: string) => request<{ mediaId: number; status: 'started'; playerName: string }>(`/api/media/${id}/play`, { method: 'POST', ...(playerId ? { headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ playerId }) } : {}) })
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
