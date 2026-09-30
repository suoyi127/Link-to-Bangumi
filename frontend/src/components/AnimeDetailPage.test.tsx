import { cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react'
import { afterEach, beforeEach, expect, it, vi } from 'vitest'
import * as client from '../api/client'
import type { AnimeDetail, Media } from '../api/types'
import { AnimeDetailPage } from './AnimeDetailPage'

afterEach(() => { cleanup(); vi.restoreAllMocks() })
beforeEach(() => {
  vi.spyOn(client, 'getPlayers').mockResolvedValue({ selectedId: 'mpv', items: [
    { id: 'mpv', name: 'mpv', executable: 'X:/mpv.exe', available: true },
    { id: 'vlc', name: 'VLC', executable: 'X:/vlc.exe', available: true },
  ] })
  vi.stubGlobal('matchMedia', vi.fn().mockImplementation(() => ({ matches: false, addListener: vi.fn(), removeListener: vi.fn(), addEventListener: vi.fn(), removeEventListener: vi.fn() })))
  const computedStyle = window.getComputedStyle.bind(window)
  vi.spyOn(window, 'getComputedStyle').mockImplementation((element) => computedStyle(element))
})
const media = (id: number): Media => ({ id, scanId: 1, sourcePath: `/source/${id}.mkv`, filename: `${id}.mkv`, parsedTitle: '本地番剧', title: '本地番剧', season: '', episodeNumber: String(id), episodeType: 'normal', sizeBytes: 1, status: 'organized', origin: 'external_import', animeId: 7, confidence: 1 })
const detail = (items: Media[], nextMediaOffset: number | null): AnimeDetail => ({ id: 7, displayTitle: '本地番剧', originalTitle: '', season: '', coverUrl: '', locked: false, aliases: ['别名甲'], media: items, nextMediaOffset })
it('temporarily selects VLC without saving a new default', async () => {
  vi.spyOn(client, 'getAnimeDetail').mockResolvedValue(detail([media(1)], null))
  vi.spyOn(client, 'getAuditLogs').mockResolvedValue({ items: [], nextOffset: null })
  const save = vi.spyOn(client, 'savePlayer')
  const play = vi.spyOn(client, 'playMedia').mockResolvedValue({ mediaId: 1, status: 'started', playerName: 'VLC' })
  render(<AnimeDetailPage animeId={7} onBack={vi.fn()} />)
  await screen.findByRole('option', { name: 'VLC' })
  fireEvent.change(screen.getByLabelText('本次播放器'), { target: { value: 'vlc' } })
  fireEvent.click(screen.getByRole('button', { name: '播放 1.mkv' }))
  await waitFor(() => expect(play).toHaveBeenCalledWith(1, 'vlc'))
  expect(await screen.findByText('已启动VLC：1.mkv')).toBeInTheDocument()
  expect(save).not.toHaveBeenCalled()
}, 20000)
function deferred<T>() { let resolve!: (value: T) => void; const promise = new Promise<T>((done) => { resolve = done }); return { promise, resolve } }

it('shows the canonical anime title and episode instead of the source path', async () => {
  const item = { ...media(3), title: '规范番剧名', episodeNumber: '03', filename: 'release.mkv', sourcePath: 'C:\\downloads\\release.mkv' }
  vi.spyOn(client, 'getAnimeDetail').mockResolvedValue(detail([item], null))
  vi.spyOn(client, 'getAuditLogs').mockResolvedValue({ items: [], nextOffset: null })
  render(<AnimeDetailPage animeId={7} onBack={vi.fn()} />)
  expect(await screen.findByText('本地番剧 [03]')).toBeInTheDocument()
  expect(screen.getByText('release.mkv')).toBeInTheDocument()
  expect(screen.queryByText(item.sourcePath)).not.toBeInTheDocument()
})

it('uses the anime canonical title for romanized or alias media rows', async () => {
  const canonical = '与奔驰于透明之夜的你，谈一场看不见的恋爱。'
  const rows = [
    { ...media(3), title: 'Toumei na Yoru ni Kakeru Kimi to', episodeNumber: '03', filename: 'roman-03.mkv' },
    { ...media(4), title: '透明な夜に駆ける君と', episodeNumber: '04', filename: 'alias-04.mkv' },
  ]
  vi.spyOn(client, 'getAnimeDetail').mockResolvedValue({ ...detail(rows, null), displayTitle: canonical })
  vi.spyOn(client, 'getAuditLogs').mockResolvedValue({ items: [], nextOffset: null })
  render(<AnimeDetailPage animeId={7} onBack={vi.fn()} />)
  expect(await screen.findByText(`${canonical} [03]`)).toBeInTheDocument()
  expect(screen.getByText(`${canonical} [04]`)).toBeInTheDocument()
  expect(screen.getByText('roman-03.mkv')).toBeInTheDocument()
  expect(screen.getByText('alias-04.mkv')).toBeInTheDocument()
})

it('falls back to the anime title when the media title and episode are blank', async () => {
  vi.spyOn(client, 'getAnimeDetail').mockResolvedValue(detail([{ ...media(4), title: ' ', episodeNumber: ' ' }], null))
  vi.spyOn(client, 'getAuditLogs').mockResolvedValue({ items: [], nextOffset: null })
  render(<AnimeDetailPage animeId={7} onBack={vi.fn()} />)
  await screen.findByText('4.mkv')
  expect(screen.getAllByText('本地番剧')).toHaveLength(2)
  expect(screen.queryByText('本地番剧 []')).not.toBeInTheDocument()
})

it('starts local mpv for the selected media ID and reports launch errors', async () => {
  vi.spyOn(client, 'getAnimeDetail').mockResolvedValue(detail([media(1), media(2)], null))
  vi.spyOn(client, 'getAuditLogs').mockResolvedValue({ items: [], nextOffset: null })
  const play = vi.spyOn(client, 'playMedia').mockRejectedValue(new client.ApiError('mpv_launch_failed', 503))
  render(<AnimeDetailPage animeId={7} onBack={vi.fn()} />)
  await screen.findByText('2.mkv')
  fireEvent.click(screen.getByRole('button', { name: '播放 2.mkv' }))
  await waitFor(() => expect(play).toHaveBeenCalledWith(2))
  expect(await screen.findByText(/mpv_launch_failed/)).toBeInTheDocument()
}, 20000)

it('loads media continuation beyond 500 and keeps local detail when remote search fails', async () => {
  const get = vi.spyOn(client, 'getAnimeDetail').mockResolvedValueOnce(detail([media(1)], 500)).mockResolvedValueOnce(detail([media(501)], null))
  vi.spyOn(client, 'getAuditLogs').mockResolvedValue({ items: [], nextOffset: null })
  vi.spyOn(client, 'searchBangumi').mockRejectedValue(new client.ApiError('bangumi_unavailable', 503))
  render(<AnimeDetailPage animeId={7} onBack={vi.fn()} />)
  expect(await screen.findByText('别名甲')).toBeInTheDocument()
  fireEvent.click(screen.getByRole('button', { name: '加载更多媒体' }))
  await waitFor(() => expect(get).toHaveBeenLastCalledWith(7, 200, 500))
  expect(await screen.findByText('501.mkv')).toBeInTheDocument()
  fireEvent.change(screen.getByLabelText('Bangumi 关键词'), { target: { value: '本地番剧' } })
  fireEvent.click(screen.getByRole('button', { name: '搜索 Bangumi' }))
  expect(await screen.findByText(/bangumi_unavailable/)).toBeInTheDocument()
  expect(screen.getByText('别名甲')).toBeInTheDocument()
}, 20000)

it('requires explicit binding confirmation then refreshes local detail', async () => {
  const get = vi.spyOn(client, 'getAnimeDetail').mockResolvedValue(detail([media(1)], null))
  vi.spyOn(client, 'getAuditLogs').mockResolvedValue({ items: [], nextOffset: null })
  vi.spyOn(client, 'searchBangumi').mockResolvedValue({ items: [{ id: 44, name: '候选', nameCn: '候选中文', date: '2025-01-01', coverUrl: '', episodeCount: 12, type: 2, score: 0.9 }], autoBindEligible: true, fromCache: false, localMatch: null })
  const bind = vi.spyOn(client, 'bindBangumi').mockResolvedValue({ ...detail([media(1)], null), bangumiSubjectId: 44 })
  render(<AnimeDetailPage animeId={7} onBack={vi.fn()} />)
  await screen.findByText('别名甲')
  fireEvent.change(screen.getByLabelText('Bangumi 关键词'), { target: { value: '候选' } })
  fireEvent.click(screen.getByRole('button', { name: '搜索 Bangumi' }))
  fireEvent.click(await screen.findByRole('button', { name: '绑定 候选中文' }))
  expect(bind).not.toHaveBeenCalled()
  fireEvent.click(screen.getByRole('button', { name: '确认绑定' }))
  await waitFor(() => expect(bind).toHaveBeenCalledWith(7, 44))
  await waitFor(() => expect(get).toHaveBeenCalledTimes(2))
}, 20000)

it('keeps local detail visible when audit history is unavailable', async () => {
  vi.spyOn(client, 'getAnimeDetail').mockResolvedValue(detail([media(1)], null))
  vi.spyOn(client, 'getAuditLogs').mockRejectedValue(new client.ApiError('network_error', 0))
  render(<AnimeDetailPage animeId={7} onBack={vi.fn()} />)
  expect(await screen.findByText('别名甲')).toBeInTheDocument()
  expect(screen.getByText('1.mkv')).toBeInTheDocument()
}, 20000)

it('offers a Bangumi title search when a local anime has no subject binding', async () => {
  vi.spyOn(client, 'getAnimeDetail').mockResolvedValue(detail([media(1)], null))
  vi.spyOn(client, 'getAuditLogs').mockResolvedValue({ items: [], nextOffset: null })
  render(<AnimeDetailPage animeId={7} onBack={vi.fn()} />)
  const link = await screen.findByRole('link', { name: '在 Bangumi 搜索本地番剧' })
  expect(link).toHaveAttribute('href', 'https://bgm.tv/subject_search/%E6%9C%AC%E5%9C%B0%E7%95%AA%E5%89%A7?cat=2')
  expect(link).toHaveAttribute('rel', 'noopener noreferrer')
}, 20000)

it('refreshes a bound anime cover and displays the cached image', async () => {
  const original = { ...detail([media(1)], null), bangumiSubjectId: 123 }
  const cached = { ...original, coverUrl: '/api/covers/7/123/0123456789abcdef.jpg' }
  vi.spyOn(client, 'getAnimeDetail').mockResolvedValueOnce(original).mockResolvedValueOnce(cached)
  vi.spyOn(client, 'getAuditLogs').mockResolvedValue({ items: [], nextOffset: null })
  const refresh = vi.spyOn(client, 'refreshAnimeCover').mockResolvedValue({ coverUrl: cached.coverUrl })
  render(<AnimeDetailPage animeId={7} onBack={vi.fn()} />)
  fireEvent.click(await screen.findByRole('button', { name: '刮削封面' }))
  await waitFor(() => expect(refresh).toHaveBeenCalledWith(7))
  expect(await screen.findByRole('img', { name: '本地番剧 封面' })).toHaveAttribute('src', cached.coverUrl)
}, 20000)

it('ignores an in-flight media continuation after binding refreshes detail', async () => {
  const oldPage = deferred<AnimeDetail>()
  const get = vi.spyOn(client, 'getAnimeDetail').mockResolvedValueOnce(detail([media(1)], 200)).mockImplementationOnce(() => oldPage.promise).mockResolvedValueOnce({ ...detail([media(2)], null), bangumiSubjectId: 44 })
  vi.spyOn(client, 'getAuditLogs').mockResolvedValue({ items: [], nextOffset: null })
  vi.spyOn(client, 'searchBangumi').mockResolvedValue({ items: [{ id: 44, name: '候选', nameCn: '候选', date: '', coverUrl: '', episodeCount: 12, type: 2, score: 0.9 }], autoBindEligible: false, fromCache: false, localMatch: null })
  vi.spyOn(client, 'bindBangumi').mockResolvedValue({ ...detail([media(2)], null), bangumiSubjectId: 44 })
  render(<AnimeDetailPage animeId={7} onBack={vi.fn()} />)
  await screen.findByText('1.mkv')
  fireEvent.click(screen.getByRole('button', { name: '加载更多媒体' }))
  await waitFor(() => expect(get).toHaveBeenCalledTimes(2))
  fireEvent.change(screen.getByLabelText('Bangumi 关键词'), { target: { value: '候选' } })
  fireEvent.click(screen.getByRole('button', { name: '搜索 Bangumi' }))
  fireEvent.click(await screen.findByRole('button', { name: '绑定 候选' }))
  fireEvent.click(screen.getByRole('button', { name: '确认绑定' }))
  await screen.findByText('2.mkv')
  oldPage.resolve(detail([media(999)], 400))
  await waitFor(() => expect(get).toHaveBeenCalledTimes(3))
  expect(screen.queryByText('999.mkv')).not.toBeInTheDocument()
  expect(screen.queryByRole('button', { name: '加载更多媒体' })).not.toBeInTheDocument()
}, 20000)

it('does not carry audit or busy state across animeId changes', async () => {
  const oldPage = deferred<AnimeDetail>()
  const get = vi.spyOn(client, 'getAnimeDetail').mockResolvedValueOnce(detail([media(1)], 200)).mockImplementationOnce(() => oldPage.promise).mockResolvedValueOnce({ ...detail([media(2)], 200), id: 8, aliases: ['新别名'] })
  vi.spyOn(client, 'getAuditLogs').mockResolvedValueOnce({ items: [{ id: 1, action: '旧审计', entityType: 'anime', entityId: '7', createdAt: 'today' }], nextOffset: null }).mockResolvedValueOnce({ items: [], nextOffset: null })
  const rendered = render(<AnimeDetailPage animeId={7} onBack={vi.fn()} />)
  expect(await screen.findByText(/旧审计/)).toBeInTheDocument()
  fireEvent.click(screen.getByRole('button', { name: '加载更多媒体' }))
  await waitFor(() => expect(get).toHaveBeenCalledTimes(2))
  rendered.rerender(<AnimeDetailPage animeId={8} onBack={vi.fn()} />)
  expect(await screen.findByText('新别名')).toBeInTheDocument()
  expect(screen.queryByText(/旧审计/)).not.toBeInTheDocument()
  expect(screen.getByRole('button', { name: '加载更多媒体' })).not.toBeDisabled()
  oldPage.resolve(detail([media(999)], null))
  expect(screen.queryByText('999.mkv')).not.toBeInTheDocument()
}, 20000)
