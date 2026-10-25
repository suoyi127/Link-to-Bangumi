import type { Page, Media, AnimeSort, Anime, AnimeDetail, AnimeCalendar, BangumiSearch, MetadataSync, AnimeMergePreview } from './types'
import { request, json, pageQuery } from './request'

export const getAnime = (limit = 50, offset = 0, signal?: AbortSignal, sort?: AnimeSort) => request<Page<Anime>>(`/api/anime${pageQuery(limit, offset)}${sort ? `&sort=${sort}` : ''}`, { signal })
export const editAnime = (id: number, edit: Pick<Anime, 'displayTitle' | 'season' | 'coverUrl'>) => request<AnimeDetail>(`/api/anime/${id}`, { ...json({ displayTitle: edit.displayTitle, season: edit.season, coverUrl: edit.coverUrl }), method: 'PUT' })
export const editAnimeMedia = (id: number, edit: Pick<Media, 'episodeNumber' | 'episodeType'>) => request<Media>(`/api/media/${id}/episode`, { ...json({ episodeNumber: edit.episodeNumber, episodeType: edit.episodeType }), method: 'PUT' })
export const getAnimeCalendar = (signal?: AbortSignal) => request<AnimeCalendar>('/api/anime-calendar', { signal })
export const getAnimeDetail = (id: number, mediaLimit = 50, mediaOffset = 0) => request<AnimeDetail>(`/api/anime/${id}?mediaLimit=${mediaLimit}&mediaOffset=${mediaOffset}`)
export const searchBangumi = (query: string) => request<BangumiSearch>(`/api/bangumi/search?q=${encodeURIComponent(query)}`)
export const bindBangumi = (id: number, subjectId: number) => request<AnimeDetail>(`/api/anime/${id}/bangumi`, { ...json({ subjectId, confirmed: true }), method: 'PUT' })
export const refreshAnimeCover = (id: number) => request<{ coverUrl: string }>(`/api/anime/${id}/cover/refresh`, { method: 'POST' })
export const syncMetadata = () => request<MetadataSync>('/api/enrichment/sync', { method: 'POST' })
export const previewAnimeMerge = (id: number, subjectId: number) => request<AnimeMergePreview>(`/api/anime/${id}/merge/preview`, json({ subjectId }))
export const mergeAnime = (id: number, previewId: string) => request<AnimeDetail>(`/api/anime/${id}/merge`, json({ previewId, confirmed: true }))
