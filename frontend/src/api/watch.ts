import { json, request } from './request'

export type WatchProgress = { mediaId: number; watched: boolean; position: number; duration: number; updatedAt: number }
export type ContinueEpisode = Pick<WatchProgress, 'mediaId' | 'position' | 'duration'> & {
  animeId: number; title: string; episodeNumber: string; episodeType: string; nextEpisode: boolean
}
export type WatchSync = {
  enabled: boolean; connected: boolean; username: string; pending: number; errorCode: string
  issues: { mediaId: number; title?: string; episodeNumber?: string; errorCode: string }[]
}
export const getWatchProgress = (animeId: number, signal: AbortSignal) =>
  request<{ items: WatchProgress[] }>(`/api/watch-progress?animeId=${animeId}`, { signal })
export const setWatched = (mediaId: number, watched: boolean) =>
  request<WatchProgress>(`/api/media/${mediaId}/watched`, { ...json({ watched }), method: 'PUT' })
export const getContinueEpisodes = (signal: AbortSignal) =>
  request<{ items: ContinueEpisode[] }>('/api/watch-progress/continue', { signal })
export const getWatchSync = (signal: AbortSignal) => request<WatchSync>('/api/watch-sync', { signal })
export const saveWatchSync = (enabled: boolean, token?: string) =>
  request<WatchSync>('/api/watch-sync', { ...json({ enabled, token }), method: 'PUT' })
export const collectWatchSubject = (mediaId: number) => request<WatchSync>('/api/watch-sync/collect', json({ mediaId }))
