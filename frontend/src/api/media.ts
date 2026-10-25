import type { Page, InboxOrigin, FolderImport, InboxPage, Scan, Media, Preview, Execution, Preferences, PlayerCatalog, PlaybackResult } from './types'
import { request, json, pageQuery } from './request'

async function startScan(path: string, timeoutMs = 20_000) {
  const controller = new AbortController()
  const timer = setTimeout(() => controller.abort(), timeoutMs)
  try { return await request<Scan>(path, { method: 'POST', signal: controller.signal }) }
  finally { clearTimeout(timer) }
}

export const getScans = (limit = 10, offset = 0, signal?: AbortSignal) => request<Page<Scan>>(`/api/scans${pageQuery(limit, offset)}`, { signal })
export const startSourceScan = () => startScan('/api/scans')
export const startImportScan = () => startScan('/api/imports/scan')
export const getFolderImports = () => request<{ items: FolderImport[] }>('/api/folder-imports')
export const addFolderImport = (path: string) => request<FolderImport>('/api/folder-imports', json({ path }))
export const scanFolderImport = (id: number) => startScan(`/api/folder-imports/${id}/scan`, 120_000)
export const getInbox = (limit = 100, offset = 0, signal?: AbortSignal, origin?: InboxOrigin) => request<InboxPage>(`/api/inbox${pageQuery(limit, offset)}${origin ? `&origin=${encodeURIComponent(origin)}` : ''}`, { signal })
export const correctMedia = (id: number, correction: Pick<Media, 'title' | 'season' | 'episodeNumber' | 'episodeType'>) => request<Media>(`/api/inbox/${id}/parse`, json({ title: correction.title, season: correction.season, episodeNumber: correction.episodeNumber, episodeType: correction.episodeType }))
export const getPlayers = (signal?: AbortSignal) => request<PlayerCatalog>('/api/players', { signal })
export const savePlayer = (playerId: string, executable: string) => request<PlayerCatalog>('/api/players', { method: 'PUT', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ playerId, executable }) })
export const playMedia = (id: number, playerId?: string) => request<PlaybackResult>(`/api/media/${id}/play`, { method: 'POST', ...(playerId ? { headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ playerId }) } : {}) })
export const previewOrganization = (mediaFileId: number, operation: Preferences['preferredOperation']) => request<Preview>('/api/organize/preview', json({ mediaFileId, operation }))
export const executeOrganization = (planId: number, idempotencyKey: string, qbDownloadComplete: boolean) => request<Execution>('/api/organize/execute', json({ planId, idempotencyKey, confirmed: true, qbDownloadComplete }))
