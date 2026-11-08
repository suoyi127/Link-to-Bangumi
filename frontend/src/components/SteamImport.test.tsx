import { cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react'
import { afterEach, beforeEach, expect, it, vi } from 'vitest'
import * as gamesApi from '../api/games'
import { GameLibraryPage, GameResources } from './GamePages'
const game = { id: 1, title: 'Steam Game', path: 'steam://rungameid/10', developer: '', summary: '', platform: 'PC', subjectId: null, manualMetadata: false, missing: false, coverUrl: '', vndbId: '', metadataSource: '', steamAppId: '10' }
beforeEach(() => {
  vi.stubGlobal('matchMedia', vi.fn().mockImplementation(() => ({ matches: false, addListener: vi.fn(), removeListener: vi.fn(), addEventListener: vi.fn(), removeEventListener: vi.fn() })))
  const computed = window.getComputedStyle.bind(window)
  vi.spyOn(window, 'getComputedStyle').mockImplementation((element) => computed(element))
})
afterEach(() => { cleanup(); vi.restoreAllMocks(); vi.unstubAllGlobals() })
it('saves optional Steam configuration and imports locally with a blank key', async () => {
  vi.spyOn(gamesApi, 'getGames').mockResolvedValue({ items: [] })
  const fetchMock = vi.fn().mockImplementation(async (_url: string, init?: RequestInit) => ({ ok: true, headers: new Headers(), json: async () => init?.method === 'POST' ? { imported: 2, updated: 1, warnings: ['未配置统计账号'] } : { steamPath: '', steamId: '', hasApiKey: false } }))
  vi.stubGlobal('fetch', fetchMock)
  render(<GameLibraryPage />)
  fireEvent.click(screen.getByText('导入游戏', { selector: 'summary' }))
  fireEvent.click(screen.getByText('Steam 导入设置', { selector: 'summary' }))
  await screen.findByText('尚未保存 API Key')
  fireEvent.click(screen.getByRole('button', { name: '保存 Steam 设置' }))
  await waitFor(() => expect(fetchMock).toHaveBeenCalledWith('/api/steam/config', expect.objectContaining({ method: 'PUT', body: JSON.stringify({ steamPath: '', steamId: '', apiKey: '' }) })))
  fireEvent.click(screen.getByRole('button', { name: '一键导入 Steam 游戏' }))
  expect(await screen.findByText('Steam 导入完成：新增 2，更新 1。')).toBeInTheDocument()
  expect(screen.getByText('未配置统计账号')).toBeInTheDocument()
  expect(gamesApi.getGames).toHaveBeenCalledTimes(2)
})
it('keeps a saved key private, clears a replacement key, and blocks duplicate imports', async () => {
  vi.spyOn(gamesApi, 'getGames').mockResolvedValue({ items: [] })
  let finish!: (value: unknown) => void
  const fetchMock = vi.fn().mockImplementation(async (_url: string, init?: RequestInit) => init?.method === 'POST' ? new Promise((resolve) => { finish = resolve }) : { ok: true, headers: new Headers(), json: async () => ({ steamPath: 'D:/Steam', steamId: '76561198000000000', hasApiKey: true }) })
  vi.stubGlobal('fetch', fetchMock)
  render(<GameLibraryPage />)
  fireEvent.click(screen.getByText('导入游戏', { selector: 'summary' }))
  await waitFor(() => expect(screen.getByRole('button', { name: '一键导入 Steam 游戏' })).toBeEnabled())
  fireEvent.click(screen.getByText('Steam 导入设置', { selector: 'summary' }))
  await screen.findByText('已保存 API Key；留空保留原密钥')
  const key = screen.getByLabelText('Steam API Key')
  expect(key).toHaveValue('')
  expect(key).toHaveAttribute('type', 'password')
  fireEvent.change(key, { target: { value: 'replacement' } })
  fireEvent.click(screen.getByRole('button', { name: '保存 Steam 设置' }))
  await waitFor(() => expect(key).toHaveValue(''))
  fireEvent.click(screen.getByRole('button', { name: '一键导入 Steam 游戏' }))
  await waitFor(() => expect(screen.getByRole('button', { name: '正在导入并同步 Steam 统计' })).toBeDisabled())
  finish({ ok: true, headers: new Headers(), json: async () => ({ imported: 0, updated: 1, warnings: [] }) })
  expect(await screen.findByText('Steam 导入完成：新增 0，更新 1。')).toBeInTheDocument()
})
it.each([
  [null, null, null, '游玩时长：未知', '成就：未知'],
  [0, 0, 0, '游玩时长：0 分钟', '成就：0 / 0'],
  [125, 3, 10, '游玩时长：125 分钟', '成就：3 / 10'],
  [0, null, 2, '游玩时长：0 分钟', '成就：未知 / 2'],
])('shows Steam stats without treating unknown values as zero (%s)', async (playtimeMinutes, achievementsUnlocked, achievementsTotal, time, achievements) => {
  vi.spyOn(gamesApi, 'getGames').mockResolvedValue({ items: [{ ...game, playtimeMinutes, achievementsUnlocked, achievementsTotal, steamStatsError: 'steam_private_profile' } as typeof game] })
  render(<GameLibraryPage />)
  fireEvent.click(await screen.findByRole('button', { name: '查看游戏 Steam Game' }))
  expect(screen.getByText(time as string)).toBeInTheDocument()
  expect(screen.getByText(achievements as string)).toBeInTheDocument()
  expect(screen.getByText(/统计同步失败；已有数值为上次成功同步的结果/)).toBeInTheDocument()
})
it('makes generated Steam launch paths read only', async () => {
  vi.spyOn(gamesApi, 'getGames').mockResolvedValue({ items: [game] })
  render(<GameResources />)
  fireEvent.click(await screen.findByRole('button', { name: '编辑游戏 Steam Game' }))
  expect(screen.getByLabelText('游戏启动路径')).toHaveAttribute('readonly')
})
it('labels Steam metadata as Steam in the game detail', async () => {
  vi.spyOn(gamesApi, 'getGames').mockResolvedValue({ items: [{ ...game, title: '中文游戏', metadataSource: 'steam', coverUrl: '/api/games/1/cover', posterUrl: '/api/games/1/cover?view=poster' }] })
  render(<GameLibraryPage />)
  expect(await screen.findByRole('img', { name: '中文游戏 封面' })).toHaveAttribute('src', '/api/games/1/cover?view=poster')
  fireEvent.click(await screen.findByRole('button', { name: '查看游戏 中文游戏' }))
  expect(screen.getByRole('img', { name: '中文游戏 封面' })).toHaveAttribute('src', '/api/games/1/cover')
  expect(screen.getByText('资料来源：Steam')).toBeInTheDocument()
})
it('describes pending statistics as pending rather than failed', async () => {
  vi.spyOn(gamesApi, 'getGames').mockResolvedValue({ items: [{ ...game, steamStatsError: 'steam_stats_pending' }] })
  render(<GameLibraryPage />)
  fireEvent.click(await screen.findByRole('button', { name: '查看游戏 Steam Game' }))
  expect(screen.getByText('Steam 统计尚未同步；已有数值为上次成功同步的结果。')).toBeInTheDocument()
})
it('retries a failed settings load and explains import failures in Chinese', async () => {
  vi.spyOn(gamesApi, 'getGames').mockResolvedValue({ items: [] })
  const fetchMock = vi.fn()
    .mockRejectedValueOnce(new Error('offline'))
    .mockResolvedValueOnce(new Response(JSON.stringify({ steamPath: '', steamId: '', hasApiKey: false })))
    .mockResolvedValueOnce(new Response(JSON.stringify({ error: { code: 'steam_not_found' } }), { status: 400 }))
  vi.stubGlobal('fetch', fetchMock)
  render(<GameLibraryPage />)
  fireEvent.click(screen.getByText('导入游戏', { selector: 'summary' }))
  fireEvent.click(await screen.findByRole('button', { name: '重试加载 Steam 设置' }))
  await waitFor(() => expect(screen.getByRole('button', { name: '一键导入 Steam 游戏' })).toBeEnabled())
  fireEvent.click(screen.getByRole('button', { name: '一键导入 Steam 游戏' }))
  expect(await screen.findByText('未找到 Steam 安装目录，请展开设置填写实际路径。')).toBeInTheDocument()
})

