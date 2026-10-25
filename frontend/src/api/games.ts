import type { GameResource, GameCandidate, GameScrape, VndbCandidate } from './types'
import { request, json } from './request'

export const getGames = (signal?: AbortSignal) => request<{ items: GameResource[] }>('/api/games', { signal })
export const importGame = (path: string) => request<GameScrape & { game: GameResource }>('/api/game-imports', json({ path }))
export const editGame = (id: number, metadata: Pick<GameResource, 'title' | 'developer' | 'summary' | 'path'>) => request<GameResource>(`/api/games/${id}`, { ...json({ title: metadata.title, developer: metadata.developer, summary: metadata.summary, path: metadata.path }), method: 'PUT' })
export const removeUnavailableGame = (id: number) => request<{ removedId: number }>(`/api/games/${id}`, { ...json({ confirmed: true }), method: 'DELETE' })
export const launchGame = (id: number) => request<{ started: boolean }>(`/api/games/${id}/launch`, json({ confirmed: true }))
export const searchGameBangumi = (query: string) => request<{ items: GameCandidate[] }>(`/api/game-bangumi/search?q=${encodeURIComponent(query)}`)
export const scrapeGame = (id: number, subjectId?: number) => request<GameScrape>(`/api/games/${id}/scrape`, json(subjectId === undefined ? {} : { subjectId }))
export const searchGameVndb = (query: string) => request<{ items: VndbCandidate[]; more: boolean }>(`/api/game-vndb/search?q=${encodeURIComponent(query)}`)
export const scrapeGameVndb = (id: number, vndbId?: string) => request<GameScrape>(`/api/games/${id}/vndb/scrape`, json(vndbId === undefined ? {} : { vndbId }))
