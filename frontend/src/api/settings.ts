import type { Page, Preferences, Settings, BangumiConfig, BangumiConnectionTest, AuditLog } from './types'
import { request, json, pageQuery } from './request'

export const getSettings = (signal?: AbortSignal) => request<Settings>('/api/settings', signal ? { signal } : undefined)
export const putQbDownloadDirectory = (path: string) => request<Settings & { restartRequired: boolean }>('/api/settings/qb-download-directory', { ...json({ path }), method: 'PUT' })
export const getBangumiConfig = (signal?: AbortSignal) => request<BangumiConfig>('/api/bangumi/config', signal ? { signal } : undefined)
export const putBangumiConfig = (userAgent: string) => request<BangumiConfig>('/api/bangumi/config', { ...json({ userAgent }), method: 'PUT' })
export const testBangumiConfig = (userAgent: string) => request<BangumiConnectionTest>('/api/bangumi/config/test', json({ userAgent }))
export const deleteBangumiConfig = () => request<BangumiConfig>('/api/bangumi/config', { method: 'DELETE' })
export const putSettings = (preferences: Preferences) => request<Settings>('/api/settings', { ...json({ preferredOperation: preferences.preferredOperation, scanIntervalSeconds: preferences.scanIntervalSeconds, mpvExecutable: preferences.mpvExecutable, qbWebUiUrl: preferences.qbWebUiUrl }), method: 'PUT' })
export const getAuditLogs = (limit = 50, offset = 0, animeId?: number, signal?: AbortSignal) => request<Page<AuditLog>>(`/api/audit-logs${pageQuery(limit, offset)}${animeId === undefined ? '' : `&animeId=${animeId}`}`, signal ? { signal } : undefined)
