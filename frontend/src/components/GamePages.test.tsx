import { cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react'
import { afterEach, beforeEach, expect, it, vi } from 'vitest'
import * as gamesApi from '../api/games'
import { GameLibraryPage, GameResources } from './GamePages'
const game = { id: 1, title: '中文游戏', path: 'D:/Games/Story.exe', developer: '开发商', summary: '简介', platform: 'PC', subjectId: 123, manualMetadata: false, missing: false, coverUrl: '', vndbId: '', metadataSource: 'bangumi' }
beforeEach(() => {
  vi.stubGlobal('matchMedia', vi.fn().mockImplementation(() => ({ matches: false, addListener: vi.fn(), removeListener: vi.fn(), addEventListener: vi.fn(), removeEventListener: vi.fn() })))
  const computed = window.getComputedStyle.bind(window)
  vi.spyOn(window, 'getComputedStyle').mockImplementation((element) => computed(element))
})
afterEach(() => { cleanup(); vi.restoreAllMocks(); vi.unstubAllGlobals() })
it('imports an executable directly from the game library without launching it', async () => {
  vi.spyOn(gamesApi, 'getGames').mockResolvedValue({ items: [] })
  const add = vi.spyOn(gamesApi, 'importGame').mockResolvedValue({ game, bound: true, coverUpdated: true, errorCode: '', source: 'bangumi' })
  const launch = vi.spyOn(gamesApi, 'launchGame').mockResolvedValue({ started: true })
  render(<GameLibraryPage />)
  fireEvent.click(screen.getByText('导入游戏', { selector: 'summary' }))
  fireEvent.change(screen.getByLabelText('游戏可执行文件路径'), { target: { value: game.path } })
  fireEvent.click(screen.getByRole('button', { name: '导入游戏' }))
  await waitFor(() => expect(add).toHaveBeenCalledWith(game.path))
  expect(await screen.findByText('Bangumi 绑定与封面刮削完成。')).toBeInTheDocument()
  expect(launch).not.toHaveBeenCalled()
})
it('requires launch confirmation and disables missing games', async () => {
  vi.spyOn(gamesApi, 'getGames').mockResolvedValue({ items: [game, { ...game, id: 2, title: '缺失游戏', missing: true }] })
  const launch = vi.spyOn(gamesApi, 'launchGame').mockResolvedValue({ started: true })
  render(<GameLibraryPage />)
  fireEvent.click(await screen.findByRole('button', { name: '查看游戏 缺失游戏' }))
  expect(screen.getByRole('button', { name: '启动游戏' })).toBeDisabled()
  fireEvent.click(screen.getByRole('button', { name: '返回游戏库' }))
  fireEvent.click(screen.getByRole('button', { name: '查看游戏 中文游戏' }))
  fireEvent.click(screen.getByRole('button', { name: '启动游戏' }))
  expect(launch).not.toHaveBeenCalled()
  const buttons = screen.getAllByRole('button', { name: '启动游戏' })
  fireEvent.click(buttons[buttons.length - 1])
  await waitFor(() => expect(launch).toHaveBeenCalledWith(1))
})
it('edits game metadata in resources and hides deletion for accessible games', async () => {
  vi.spyOn(gamesApi, 'getGames').mockResolvedValue({ items: [game] })
  const save = vi.spyOn(gamesApi, 'editGame').mockResolvedValue({ ...game, title: '新游戏标题', manualMetadata: true })
  render(<GameResources />)
  fireEvent.click(await screen.findByRole('button', { name: '编辑游戏 中文游戏' }))
  expect(screen.queryByRole('button', { name: '删除游戏 中文游戏' })).not.toBeInTheDocument()
  fireEvent.change(screen.getByLabelText('游戏标题'), { target: { value: '新游戏标题' } })
  fireEvent.click(screen.getByRole('button', { name: '保存游戏信息' }))
  await waitFor(() => expect(save).toHaveBeenCalledWith(1, expect.objectContaining({ title: '新游戏标题', path: game.path })))
})
it('searches VNDB candidates and binds a VNDB string ID', async () => {
  vi.spyOn(gamesApi, 'getGames').mockResolvedValue({ items: [game] })
  const search = vi.spyOn(gamesApi, 'searchGameVndb').mockResolvedValue({ items: [{ id: 'v4', name: 'Story', nameCn: 'VN中文游戏', platform: 'win' }], more: false })
  const bind = vi.spyOn(gamesApi, 'scrapeGameVndb').mockResolvedValue({ bound: true, coverUpdated: true, errorCode: '', source: 'vndb' })
  render(<GameResources />)
  fireEvent.click(await screen.findByRole('button', { name: '编辑游戏 中文游戏' }))
  fireEvent.change(screen.getByLabelText('游戏刮削来源'), { target: { value: 'vndb' } })
  fireEvent.click(screen.getByRole('button', { name: '搜索游戏条目' }))
  await waitFor(() => expect(search).toHaveBeenCalledWith('中文游戏'))
  expect(await screen.findByRole('link', { name: 'VN中文游戏' })).toHaveAttribute('href', 'https://vndb.org/v4')
  fireEvent.click(screen.getByRole('button', { name: '绑定此条目' }))
  await waitFor(() => expect(bind).toHaveBeenCalledWith(1, 'v4'))
  expect(await screen.findByText('VNDB 绑定与封面刮削完成。')).toBeInTheDocument()
})

it.each([
  { bound: true, coverUpdated: false, errorCode: 'cover_failed', message: '已绑定 Bangumi，封面暂未更新（cover_failed）。' },
  { bound: false, coverUpdated: false, errorCode: 'bangumi_unavailable', message: '游戏已登记，刮削暂未完成（bangumi_unavailable）；可在游戏资源中重新刮削。' },
])('shows game scrape outcome for $errorCode without losing a partial success', async (result) => {
  vi.spyOn(gamesApi, 'getGames').mockResolvedValue({ items: [game] })
  vi.spyOn(gamesApi, 'scrapeGame').mockResolvedValue({ ...result, source: 'bangumi' })
  render(<GameResources />)
  fireEvent.click(await screen.findByRole('button', { name: '编辑游戏 中文游戏' }))
  fireEvent.click(screen.getByRole('button', { name: '自动匹配／刷新游戏刮削' }))
  expect(await screen.findByText(result.message)).toBeInTheDocument()
  expect(screen.getByLabelText('游戏标题')).toHaveValue(game.title)
})
