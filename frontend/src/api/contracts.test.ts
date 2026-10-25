import { afterEach, expect, expectTypeOf, it, vi } from 'vitest'
import { correctMedia, playMedia, getInbox } from './media'
import { editAnime, editAnimeMedia, bindBangumi, getAnime } from './anime'
import { editNovel, saveNovelReader, importNovels, readNovel, getNovels } from './novels'
import { editGame, importGame, getGames, scrapeGame } from './games'
import { putQbConfig, testQbConfig, createMikanRule, getQbConfig, getQbStatus, getMikanFeeds } from './qb'
import { putSettings, putBangumiConfig, getSettings, getBangumiConfig, getAuditLogs } from './settings'

afterEach(() => vi.unstubAllGlobals())

const readonlyFields = { id: 999, requestId: 'response-only', sourcePath: '/private', extra: true }
const correction = { title: '修正', season: '1', episodeNumber: '2', episodeType: 'normal' }
const anime = { displayTitle: '番剧', season: '1', coverUrl: '' }
const episode = { episodeNumber: '2', episodeType: 'normal' }
const novel = { title: '小说', author: '作者', summary: '' }
const game = { title: '游戏', developer: '作者', summary: '', path: 'D:/game.exe' }
const qb = { url: 'http://localhost:8080', username: 'user', password: 'secret' }
const rule = { ruleName: '订阅', feedUrl: 'https://mikanani.me/RSS/Bangumi?bangumiId=1', keyword: '字幕' }
const preferences = { preferredOperation: 'copy' as const, scanIntervalSeconds: 30, mpvExecutable: '', qbWebUiUrl: '' }
const reader = { type: 'system' as const, executable: '' }

it.each([
  ['correctMedia', () => correctMedia(7, { ...correction, ...readonlyFields }), '/api/inbox/7/parse', 'POST', correction],
  ['editAnime', () => editAnime(7, { ...anime, ...readonlyFields }), '/api/anime/7', 'PUT', anime],
  ['editAnimeMedia', () => editAnimeMedia(7, { ...episode, ...readonlyFields }), '/api/media/7/episode', 'PUT', episode],
  ['editNovel', () => editNovel(7, { ...novel, ...readonlyFields }), '/api/novels/7', 'PUT', novel],
  ['editGame', () => editGame(7, { ...game, ...readonlyFields }), '/api/games/7', 'PUT', game],
  ['putQbConfig', () => putQbConfig({ ...qb, ...readonlyFields }), '/api/qb/config', 'PUT', qb],
  ['testQbConfig', () => testQbConfig({ ...qb, ...readonlyFields }), '/api/qb/config/test', 'POST', qb],
  ['createMikanRule', () => createMikanRule({ ...rule, ...readonlyFields }), '/api/qb/rss/rules', 'POST', rule],
  ['putSettings', () => putSettings({ ...preferences, ...readonlyFields }), '/api/settings', 'PUT', preferences],
  ['saveNovelReader', () => saveNovelReader({ ...reader, ...readonlyFields }), '/api/novel-reader', 'PUT', reader],
] as const)('%s submits only its writable fields', async (_name, write, path, method, expected) => {
  const fetchMock = vi.fn().mockResolvedValue(new Response('{}'))
  vi.stubGlobal('fetch', fetchMock)
  await write()
  expect(fetchMock).toHaveBeenCalledWith(path, expect.objectContaining({ method, headers: { Accept: 'application/json', 'Content-Type': 'application/json' }, body: JSON.stringify(expected) }))
})

it.each([
  ['game import', () => importGame('D:/game.exe'), '/api/game-imports', 'POST', { path: 'D:/game.exe' }],
  ['novel import', () => importNovels('D:/books'), '/api/novel-imports', 'POST', { path: 'D:/books' }],
  ['anime binding', () => bindBangumi(7, 42), '/api/anime/7/bangumi', 'PUT', { subjectId: 42, confirmed: true }],
  ['Bangumi config', () => putBangumiConfig('app/contact'), '/api/bangumi/config', 'PUT', { userAgent: 'app/contact' }],
] as const)('%s retains its path, method and body', async (_name, write, path, method, expected) => {
  const fetchMock = vi.fn().mockResolvedValue(new Response('{}'))
  vi.stubGlobal('fetch', fetchMock)
  await write()
  expect(fetchMock).toHaveBeenCalledWith(path, expect.objectContaining({ method, body: JSON.stringify(expected) }))
})

it.each([['novel read', () => readNovel(7), '/api/novel-files/7/read'], ['default player', () => playMedia(7), '/api/media/7/play']] as const)('%s uses a body-free POST', async (_name, write, path) => {
  const fetchMock = vi.fn().mockResolvedValue(new Response('{}'))
  vi.stubGlobal('fetch', fetchMock)
  await write()
  expect(fetchMock).toHaveBeenCalledWith(path, { method: 'POST', headers: { Accept: 'application/json' } })
})


it.each([
  ['settings', getSettings, '/api/settings'],
  ['Bangumi config', getBangumiConfig, '/api/bangumi/config'],
  ['qB config', getQbConfig, '/api/qb/config'],
  ['qB status', getQbStatus, '/api/qb/status'],
  ['Mikan feeds', getMikanFeeds, '/api/qb/rss'],
  ['audit', (signal?: AbortSignal) => getAuditLogs(50, 0, undefined, signal), '/api/audit-logs?limit=50&offset=0'],
] as const)('%s GET forwards cancellation while retaining its default request shape', async (_name, read, path) => {
  const fetchMock = vi.fn().mockImplementation(() => Promise.resolve(new Response('{}')))
  vi.stubGlobal('fetch', fetchMock)
  await read()
  expect(fetchMock).toHaveBeenLastCalledWith(path, { headers: { Accept: 'application/json' } })
  const controller = new AbortController()
  await read(controller.signal)
  expect(fetchMock).toHaveBeenLastCalledWith(path, { headers: { Accept: 'application/json' }, signal: controller.signal })
})

it('exposes response metadata separately from nested records and keeps nullable and optional fields distinct', () => {
  type AnimeResponse = Awaited<ReturnType<typeof getAnime>>
  type NovelResponse = Awaited<ReturnType<typeof getNovels>>
  type GamesResponse = Awaited<ReturnType<typeof getGames>>
  type InboxResponse = Awaited<ReturnType<typeof getInbox>>
  expectTypeOf<AnimeResponse['requestId']>().toEqualTypeOf<string | undefined>()
  expectTypeOf<Extract<keyof AnimeResponse['items'][number], 'requestId'>>().toEqualTypeOf<never>()
  expectTypeOf<Extract<keyof NovelResponse['items'][number], 'requestId'>>().toEqualTypeOf<never>()
  expectTypeOf<Extract<keyof GamesResponse['items'][number], 'requestId'>>().toEqualTypeOf<never>()
  expectTypeOf<AnimeResponse['nextOffset']>().toEqualTypeOf<number | null>()
  expectTypeOf<AnimeResponse['items'][number]['year']>().toEqualTypeOf<number | undefined>()
  expectTypeOf<AnimeResponse['items'][number]['bangumiSubjectId']>().toEqualTypeOf<number | undefined>()
  expectTypeOf<InboxResponse['items'][number]['animeId']>().toEqualTypeOf<number | null>()
  expectTypeOf<NovelResponse['items'][number]['subjectId']>().toEqualTypeOf<number | null>()
  expectTypeOf<GamesResponse['items'][number]['subjectId']>().toEqualTypeOf<number | null>()
  expectTypeOf<Awaited<ReturnType<typeof playMedia>>['playerId']>().toEqualTypeOf<string>()
  expectTypeOf<Awaited<ReturnType<typeof importNovels>>['queuedCount']>().toEqualTypeOf<number>()
  expectTypeOf<Awaited<ReturnType<typeof scrapeGame>>['source']>().toEqualTypeOf<string>()
})
