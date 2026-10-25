import type { NovelWork, NovelSource, NovelReader, NovelCandidate, NovelScrape, NovelImport } from './types'
import { request, json } from './request'

export const getNovels = (signal?: AbortSignal) => request<{ items: NovelWork[] }>('/api/novels', { signal })
export const getNovelSources = () => request<{ items: NovelSource[] }>('/api/novel-sources')
export const importNovels = (path: string) => request<NovelImport>('/api/novel-imports', json({ path }))
export const getNovelReader = () => request<NovelReader>('/api/novel-reader')
export const saveNovelReader = (config: NovelReader) => request<NovelReader>('/api/novel-reader', { ...json({ type: config.type, executable: config.executable }), method: 'PUT' })
export const editNovel = (id: number, metadata: Pick<NovelWork, 'title' | 'author' | 'summary'>) => request<NovelWork>(`/api/novels/${id}`, { ...json({ title: metadata.title, author: metadata.author, summary: metadata.summary }), method: 'PUT' })
export const editNovelFile = (id: number, workId: number, label: string) => request<NovelWork>(`/api/novel-files/${id}`, { ...json({ workId, label }), method: 'PUT' })
export const removeUnavailableNovel = (id: number) => request<{ removedId: number }>(`/api/novels/${id}`, { ...json({ confirmed: true }), method: 'DELETE' })
export const readNovel = (id: number) => request<{ started: boolean }>(`/api/novel-files/${id}/read`, { method: 'POST' })
export const searchNovelBangumi = (query: string) => request<{ items: NovelCandidate[] }>(`/api/novel-bangumi/search?q=${encodeURIComponent(query)}`)
export const scrapeNovel = (id: number, file = false, subjectId?: number) => request<NovelScrape>(`/api/${file ? 'novel-files' : 'novels'}/${id}/scrape`, json(subjectId ? { subjectId } : {}))
