import { cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react'
import { afterEach, beforeEach, expect, it, vi } from 'vitest'
import * as client from '../api/client'
import { NovelLibraryPage } from './NovelLibraryPage'
import { NovelSettings } from './NovelSettings'
import { NovelResources } from './NovelResources'

const work = { id: 1, title: '中文小说', author: '作者', summary: '简介', subjectId: 123, manualMetadata: false, coverUrl: '', files: [{ id: 2, workId: 1, label: '第一卷', path: 'D:/Books/one.epub', missing: false, subjectId: null, coverUrl: '' }, { id: 3, workId: 1, label: '第二卷', path: 'D:/Books/two.epub', missing: true, subjectId: null, coverUrl: '' }] }
afterEach(() => { cleanup(); vi.restoreAllMocks() })
it('offers deletion only for a work with no accessible files and requires confirmation', async () => {
  const unavailable = { ...work, files: work.files.map((f) => ({ ...f, missing: true })) }
  const get = vi.spyOn(client, 'getNovels').mockResolvedValue({ items: [work] })
  const view = render(<NovelResources />)
  await screen.findByRole('button', { name: '编辑 中文小说' })
  expect(screen.queryByRole('button', { name: '删除词条 中文小说' })).not.toBeInTheDocument()
  view.unmount()
  get.mockResolvedValue({ items: [unavailable] })
  render(<NovelResources />)
  fireEvent.click(await screen.findByRole('button', { name: '删除词条 中文小说' }))
  expect(screen.getByText(/不会删除磁盘文件/)).toBeInTheDocument()
  const fetch = vi.spyOn(globalThis, 'fetch').mockResolvedValue(new Response(JSON.stringify({ removedId: 1 }), { status: 200 }))
  get.mockResolvedValue({ items: [] })
  fireEvent.click(screen.getByRole('button', { name: '确认删除' }))
  await waitFor(() => expect(fetch).toHaveBeenCalledWith('/api/novels/1', expect.objectContaining({ method: 'DELETE', body: JSON.stringify({ confirmed: true }) })))
  await waitFor(() => expect(screen.queryByRole('button', { name: '编辑 中文小说' })).not.toBeInTheDocument())
})
beforeEach(() => {
  vi.stubGlobal('matchMedia', vi.fn().mockImplementation(() => ({ matches: false, addListener: vi.fn(), removeListener: vi.fn(), addEventListener: vi.fn(), removeEventListener: vi.fn() })))
  const computedStyle = window.getComputedStyle.bind(window)
  vi.spyOn(window, 'getComputedStyle').mockImplementation((element) => computedStyle(element))
})
it('shows one novel work and offers reading only for available volumes', async () => {
  vi.spyOn(client, 'getNovels').mockResolvedValue({ items: [work] })
  const read = vi.spyOn(client, 'readNovel').mockResolvedValue({ started: true })
  render(<NovelLibraryPage />)
  fireEvent.click(await screen.findByRole('button', { name: '查看 中文小说' }))
  expect(screen.getByText('第一卷')).toBeInTheDocument()
  expect(screen.getByText('第二卷')).toBeInTheDocument()
  const buttons = screen.getAllByRole('button', { name: '阅读' })
  expect(buttons[1]).toBeDisabled()
  fireEvent.click(buttons[0])
  await waitFor(() => expect(read).toHaveBeenCalledWith(2))
  expect(screen.queryByText('D:/Books/one.epub')).not.toBeInTheDocument()
})
it('imports a local folder and saves a reader in settings', async () => {
  vi.spyOn(client, 'getNovelReader').mockResolvedValue({ type: 'system', executable: '' })
  vi.spyOn(client, 'getNovelSources').mockResolvedValue({ items: [] })
  const save = vi.spyOn(client, 'saveNovelReader').mockResolvedValue({ type: 'custom', executable: 'D:/reader.exe' })
  const add = vi.spyOn(client, 'importNovels').mockResolvedValue({ fileCount: 2, sourceId: 1, scraping: true })
  render(<NovelSettings />)
  fireEvent.change(await screen.findByLabelText('默认阅读器'), { target: { value: 'custom' } })
  fireEvent.change(screen.getByLabelText('阅读器可执行文件'), { target: { value: 'D:/reader.exe' } })
  fireEvent.click(screen.getByRole('button', { name: '保存阅读器' }))
  await waitFor(() => expect(save).toHaveBeenCalledWith({ type: 'custom', executable: 'D:/reader.exe' }))
  fireEvent.change(screen.getByLabelText('小说文件夹或文件路径'), { target: { value: 'D:/Books' } })
  fireEvent.click(screen.getByRole('button', { name: '导入小说资源' }))
  await waitFor(() => expect(add).toHaveBeenCalledWith('D:/Books'))
  expect(await screen.findByText(/已扫描 2 个小说文件/)).toBeInTheDocument()
})
it('keeps novel metadata editing in the resource page', async () => {
  vi.spyOn(client, 'getNovels').mockResolvedValue({ items: [work] })
  const save = vi.spyOn(client, 'editNovel').mockResolvedValue({ ...work, title: '修改标题' })
  render(<NovelResources />)
  fireEvent.click(await screen.findByRole('button', { name: '编辑 中文小说' }))
  fireEvent.change(screen.getByLabelText('小说作品标题'), { target: { value: '修改标题' } })
  fireEvent.click(screen.getByRole('button', { name: '保存小说元数据' }))
  await waitFor(() => expect(save).toHaveBeenCalledWith(1, { title: '修改标题', author: '作者', summary: '简介' }))
})
it('updates missing volumes on window focus without leaving the novel detail', async () => {
  const get = vi.spyOn(client, 'getNovels').mockResolvedValueOnce({ items: [work] }).mockResolvedValue({ items: [{ ...work, files: work.files.map((f) => ({ ...f, missing: true })) }] })
  render(<NovelLibraryPage />)
  fireEvent.click(await screen.findByRole('button', { name: '查看 中文小说' }))
  expect(screen.getAllByRole('button', { name: '阅读' })[0]).toBeEnabled()
  fireEvent(window, new Event('focus'))
  await waitFor(() => expect(screen.getAllByRole('button', { name: '阅读' })[0]).toBeDisabled())
  expect(get).toHaveBeenCalledTimes(2)
  expect(screen.getByRole('heading', { name: '卷与文件' })).toBeInTheDocument()
})
