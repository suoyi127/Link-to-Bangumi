import { act, cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react'
import { afterEach, expect, it, vi } from 'vitest'
import * as client from '../api/client'
import type { Media } from '../api/types'
import { InboxPage } from './InboxPage'

afterEach(() => { cleanup(); vi.restoreAllMocks() })

const media = (origin: string): Media => ({ id: 7, scanId: 1, sourcePath: '/source/a.mkv', filename: 'a.mkv', parsedTitle: 'Toumei na Yoru', title: '旧标题', season: '', episodeNumber: '1', episodeType: 'normal', sizeBytes: 1, status: 'inbox', origin, animeId: 3, confidence: 0.7 })

function setup(origin: string) {
  vi.stubGlobal('matchMedia', vi.fn().mockImplementation(() => ({ matches: false, addListener: vi.fn(), removeListener: vi.fn(), addEventListener: vi.fn(), removeEventListener: vi.fn() })))
  const getComputedStyle = window.getComputedStyle.bind(window)
  vi.spyOn(window, 'getComputedStyle').mockImplementation((element) => getComputedStyle(element))
  vi.spyOn(client, 'getSettings').mockResolvedValue({ sourcePath: '/source', importPath: '/imports', libraryPath: '/library', dataPath: '/data', bangumiConfigured: false, qbWebUiConfigured: false, qbDownloadConfigured: true, qbDownloadDirectory: '/source', qbDownloadEnvironmentOverride: false, preferredOperation: 'hardlink', scanIntervalSeconds: 3600, mpvExecutable: '', qbWebUiUrl: '' })
  const inbox = vi.spyOn(client, 'getInbox').mockResolvedValue({ items: [media(origin)], total: 1, nextOffset: null })
  const correction = vi.spyOn(client, 'correctMedia').mockResolvedValue({ ...media(origin), title: '新标题', animeId: 9 })
  const preview = vi.spyOn(client, 'previewOrganization').mockResolvedValue({ id: 12, targetPath: '/library/new.mkv', operation: 'copy', expiresAt: '2099-01-01T00:00:00Z', conflicts: [] })
  const execute = vi.spyOn(client, 'executeOrganization').mockRejectedValueOnce(new client.ApiError('execution_in_progress', 409, 'req-1')).mockResolvedValue({ jobId: 4, targetPath: '/library/new.mkv', bytes: 1, status: 'completed' })
  const search = vi.spyOn(client, 'searchBangumi').mockResolvedValue({ items: [{ id: 44, name: '候选', nameCn: '候选中文', date: '', coverUrl: '', episodeCount: 12, type: 2, score: 0.9 }], autoBindEligible: true, fromCache: false, localMatch: { animeId: 9, displayTitle: '本地匹配' } })
  return { inbox, correction, preview, execute, search }
}

it('uses the saved preferred operation as the initial preview choice', async () => {
  const { preview } = setup('external_import')
  vi.mocked(client.getSettings).mockResolvedValue({ sourcePath: '/source', importPath: '/imports', libraryPath: '/library', dataPath: '/data', bangumiConfigured: false, qbWebUiConfigured: false, qbDownloadConfigured: true, qbDownloadDirectory: '/source', qbDownloadEnvironmentOverride: false, preferredOperation: 'copy', scanIntervalSeconds: 3600, mpvExecutable: '', qbWebUiUrl: '' })
  render(<InboxPage onOpenAnime={vi.fn()} />)
  fireEvent.click(await screen.findByRole('button', { name: '编辑 旧标题' }))
  await waitFor(() => expect(client.getSettings).toHaveBeenCalled())
  fireEvent.click(screen.getByRole('button', { name: '生成预览' }))
  await waitFor(() => expect(preview).toHaveBeenCalledWith(7, 'copy'))
}, 60000)

it('corrects only parse fields and offers local anime navigation and ranked search without binding', async () => {
  const { correction, search } = setup('external_import')
  const openAnime = vi.fn()
  render(<InboxPage onOpenAnime={openAnime} />)
  fireEvent.click(await screen.findByRole('button', { name: '编辑 旧标题' }))
  expect(screen.getByText('识别标题：Toumei na Yoru')).toBeInTheDocument()
  expect(screen.getByLabelText('规范标题')).toHaveValue('旧标题')
  fireEvent.change(screen.getByLabelText('规范标题'), { target: { value: '新标题' } })
  fireEvent.click(screen.getByRole('button', { name: '保存修正' }))
  await waitFor(() => expect(correction).toHaveBeenCalledWith(7, { title: '新标题', season: '', episodeNumber: '1', episodeType: 'normal' }))
  fireEvent.click(await screen.findByRole('button', { name: '查看本地番剧' }))
  expect(openAnime).toHaveBeenCalledWith(9)
  fireEvent.change(screen.getByLabelText('Bangumi 关键词'), { target: { value: '新标题' } })
  fireEvent.click(screen.getByRole('button', { name: '搜索 Bangumi' }))
  await waitFor(() => expect(search).toHaveBeenCalledWith('新标题'))
  expect(screen.getByText(/本地匹配：本地匹配/)).toBeInTheDocument()
  expect(screen.getByText(/候选中文/)).toBeInTheDocument()
}, 60000)

it.each(['qb_download', 'external_import'])('previews and retries the same %s plan with one key', async (origin) => {
  const { inbox, preview, execute } = setup(origin)
  render(<InboxPage onOpenAnime={vi.fn()} />)
  fireEvent.click(await screen.findByRole('button', { name: '编辑 旧标题' }))
  fireEvent.click(screen.getByRole('button', { name: '生成预览' }))
  await waitFor(() => expect(preview).toHaveBeenCalledWith(7, 'hardlink'))
  expect(screen.getByText(/目标：\/library\/new.mkv/)).toBeInTheDocument()
  fireEvent.click(screen.getByRole('button', { name: '确认执行' }))
  if (origin === 'qb_download') {
    expect(screen.getByRole('button', { name: '执行整理' })).toBeDisabled()
    fireEvent.click(screen.getByRole('checkbox', { name: '我确认 qB 下载已完成' }))
  }
  fireEvent.click(screen.getByRole('button', { name: '执行整理' }))
  await waitFor(() => expect(execute).toHaveBeenCalledTimes(1))
  expect(await screen.findByText(/execution_in_progress.*req-1/)).toBeInTheDocument()
  fireEvent.click(screen.getByRole('button', { name: '重试执行' }))
  await waitFor(() => expect(execute).toHaveBeenCalledTimes(2))
  expect(execute.mock.calls[0]).toEqual(execute.mock.calls[1])
  expect(execute.mock.calls[0][0]).toBe(12)
  expect(execute.mock.calls[0][2]).toBe(origin === 'qb_download')
  expect(execute.mock.calls[0][1]).toMatch(/^[\x21-\x7E]+$/)
  await waitFor(() => expect(inbox).toHaveBeenCalledTimes(2))
}, 60000)

it('blocks conflicted and expired plans before confirmation', async () => {
  const { preview, execute } = setup('external_import')
  preview.mockResolvedValueOnce({ id: 12, targetPath: '/library/conflict.mkv', operation: 'copy', expiresAt: '2099-01-01T00:00:00Z', conflicts: ['目标已存在'] })
    .mockResolvedValueOnce({ id: 13, targetPath: '/library/expired.mkv', operation: 'copy', expiresAt: '2000-01-01T00:00:00Z', conflicts: [] })
  render(<InboxPage onOpenAnime={vi.fn()} />)
  fireEvent.click(await screen.findByRole('button', { name: '编辑 旧标题' }))
  fireEvent.click(screen.getByRole('button', { name: '生成预览' }))
  expect(await screen.findByText('目标已存在')).toBeInTheDocument()
  expect(screen.getByRole('button', { name: '确认执行' })).toBeDisabled()
  fireEvent.click(screen.getByRole('button', { name: '生成预览' }))
  await screen.findByText(/expired.mkv/)
  expect(screen.getByRole('button', { name: '确认执行' })).toBeDisabled()
  expect(execute).not.toHaveBeenCalled()
}, 60000)

it('invalidates a preview after saving a correction', async () => {
  setup('external_import')
  render(<InboxPage onOpenAnime={vi.fn()} />)
  fireEvent.click(await screen.findByRole('button', { name: '编辑 旧标题' }))
  fireEvent.click(screen.getByRole('button', { name: '生成预览' }))
  await screen.findByText(/目标：\/library\/new.mkv/)
  fireEvent.click(screen.getByRole('button', { name: '保存修正' }))
  await screen.findByRole('button', { name: '查看本地番剧' })
  expect(screen.queryByRole('button', { name: '确认执行' })).not.toBeInTheDocument()
}, 60000)

it('invalidates a preview as soon as a correction field changes', async () => {
  const { execute } = setup('external_import')
  render(<InboxPage onOpenAnime={vi.fn()} />)
  fireEvent.click(await screen.findByRole('button', { name: '编辑 旧标题' }))
  fireEvent.click(screen.getByRole('button', { name: '生成预览' }))
  await screen.findByText(/目标：\/library\/new.mkv/)
  fireEvent.change(screen.getByLabelText('规范标题'), { target: { value: '待修正' } })
  expect(screen.queryByRole('button', { name: '确认执行' })).not.toBeInTheDocument()
  expect(execute).not.toHaveBeenCalled()
}, 60000)

it('loads the next inbox page from the server', async () => {
  const { inbox } = setup('qb_download')
  inbox.mockResolvedValueOnce({ items: [media('qb_download')], total: 101, nextOffset: 100 })
    .mockResolvedValueOnce({ items: [{ ...media('qb_download'), id: 101, title: '第101个' }], total: 101, nextOffset: null })
  render(<InboxPage onOpenAnime={vi.fn()} />)
  await screen.findByRole('button', { name: '编辑 旧标题' })
  fireEvent.click(screen.getByRole('button', { name: '下一页' }))
  await waitFor(() => expect(inbox).toHaveBeenLastCalledWith(100, 100, expect.anything(), 'qb_download'))
  expect(await screen.findByRole('button', { name: '编辑 第101个' })).toBeInTheDocument()
}, 60000)

it('separates qB and external files with independent pages and clears a switched preview', async () => {
  const { inbox } = setup('qb_download')
  const qbSecond = { ...media('qb_download'), id: 101, title: '第二页番剧', filename: 'second.mkv' }
  const imported = { ...media('external_import'), id: 201, title: '外来番剧', filename: 'imported.mkv' }
  inbox.mockResolvedValueOnce({ items: [media('qb_download')], total: 101, nextOffset: 100 })
    .mockResolvedValueOnce({ items: [qbSecond], total: 101, nextOffset: null })
    .mockResolvedValueOnce({ items: [imported], total: 1, nextOffset: null })
    .mockResolvedValueOnce({ items: [qbSecond], total: 101, nextOffset: null })
  render(<InboxPage onOpenAnime={vi.fn()} />)
  expect(await screen.findByRole('tab', { name: /qB 下载/ })).toHaveAttribute('aria-selected', 'true')
  expect(await screen.findByRole('button', { name: '编辑 旧标题' })).toBeInTheDocument()
  fireEvent.click(screen.getByRole('button', { name: '下一页' }))
  fireEvent.click(await screen.findByRole('button', { name: '编辑 第二页番剧' }))
  fireEvent.click(screen.getByRole('button', { name: '生成预览' }))
  await screen.findByText(/目标：\/library\/new.mkv/)
  fireEvent.click(screen.getByRole('tab', { name: /外来导入/ }))
  expect(await screen.findByRole('button', { name: '编辑 外来番剧' })).toBeInTheDocument()
  expect(screen.queryByRole('button', { name: '确认执行' })).not.toBeInTheDocument()
  expect(screen.queryByLabelText('规范标题')).not.toBeInTheDocument()
  expect(inbox).toHaveBeenLastCalledWith(100, 0, expect.anything(), 'external_import')
  fireEvent.click(screen.getByRole('tab', { name: /qB 下载/ }))
  expect(await screen.findByRole('button', { name: '编辑 第二页番剧' })).toBeInTheDocument()
  expect(inbox).toHaveBeenLastCalledWith(100, 100, expect.anything(), 'qb_download')
}, 30000)

it('requires a supported episode type for an unknown parse', async () => {
  const { inbox, correction } = setup('external_import')
  inbox.mockResolvedValue({ items: [{ ...media('external_import'), episodeType: 'unknown' }], total: 1, nextOffset: null })
  render(<InboxPage onOpenAnime={vi.fn()} />)
  fireEvent.click(await screen.findByRole('button', { name: '编辑 旧标题' }))
  expect(screen.getByRole('button', { name: '保存修正' })).toBeDisabled()
  expect(correction).not.toHaveBeenCalled()
}, 60000)

it('invalidates a preview when switching media', async () => {
  const { inbox, execute } = setup('external_import')
  inbox.mockResolvedValue({ items: [media('external_import'), { ...media('external_import'), id: 8, title: '另一个' }], total: 2, nextOffset: null })
  render(<InboxPage onOpenAnime={vi.fn()} />)
  fireEvent.click(await screen.findByRole('button', { name: '编辑 旧标题' }))
  fireEvent.click(screen.getByRole('button', { name: '生成预览' }))
  await screen.findByText(/目标：\/library\/new.mkv/)
  fireEvent.click(screen.getByRole('button', { name: '编辑 另一个' }))
  expect(screen.queryByRole('button', { name: '确认执行' })).not.toBeInTheDocument()
  expect(execute).not.toHaveBeenCalled()
}, 60000)

it('requires saving a changed correction before another preview', async () => {
  const { preview } = setup('external_import')
  render(<InboxPage onOpenAnime={vi.fn()} />)
  fireEvent.click(await screen.findByRole('button', { name: '编辑 旧标题' }))
  fireEvent.change(screen.getByLabelText('规范标题'), { target: { value: '草稿标题' } })
  expect(screen.getByRole('button', { name: '生成预览' })).toBeDisabled()
  expect(screen.getByText('请先保存修正再生成预览')).toBeInTheDocument()
  expect(preview).not.toHaveBeenCalled()
}, 60000)

it('does not replace the selected row when a previous correction completes', async () => {
  const { inbox, correction } = setup('external_import')
  inbox.mockResolvedValue({ items: [media('external_import'), { ...media('external_import'), id: 8, filename: 'b.mkv', title: '第二部' }], total: 2, nextOffset: null })
  let finish!: (value: Media) => void
  correction.mockImplementation(() => new Promise((resolve) => { finish = resolve }))
  render(<InboxPage onOpenAnime={vi.fn()} />)
  fireEvent.click(await screen.findByRole('button', { name: '编辑 旧标题' }))
  fireEvent.click(screen.getByRole('button', { name: '保存修正' }))
  fireEvent.click(screen.getByRole('button', { name: '编辑 第二部' }))
  await act(async () => finish({ ...media('external_import'), title: '新标题', animeId: 9 }))
  expect(screen.getByRole('heading', { name: 'b.mkv' })).toBeInTheDocument()
  expect(screen.getByLabelText('规范标题')).toHaveValue('第二部')
}, 60000)

it('ignores Bangumi results from a previously selected row', async () => {
  const { inbox, search } = setup('external_import')
  inbox.mockResolvedValue({ items: [media('external_import'), { ...media('external_import'), id: 8, filename: 'b.mkv', title: '第二部' }], total: 2, nextOffset: null })
  let finish!: (value: Awaited<ReturnType<typeof client.searchBangumi>>) => void
  search.mockImplementation(() => new Promise((resolve) => { finish = resolve }))
  render(<InboxPage onOpenAnime={vi.fn()} />)
  fireEvent.click(await screen.findByRole('button', { name: '编辑 旧标题' }))
  fireEvent.change(screen.getByLabelText('Bangumi 关键词'), { target: { value: '旧标题' } })
  fireEvent.click(screen.getByRole('button', { name: '搜索 Bangumi' }))
  fireEvent.click(screen.getByRole('button', { name: '编辑 第二部' }))
  await act(async () => finish({ items: [], autoBindEligible: false, fromCache: false, localMatch: { animeId: 7, displayTitle: '旧匹配' } }))
  expect(screen.queryByText(/旧匹配/)).not.toBeInTheDocument()
}, 60000)
