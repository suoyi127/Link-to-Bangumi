import { cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react'
import { afterEach, beforeEach, expect, it, vi } from 'vitest'
import * as client from '../api/client'
import type { Settings } from '../api/types'
import { SettingsPage } from './SettingsPage'

const settings: Settings = {
  sourcePath: 'X:/source', importPath: 'X:/imports', libraryPath: 'X:/library', dataPath: 'X:/data',
  bangumiConfigured: false, qbWebUiConfigured: false, preferredOperation: 'hardlink',
  scanIntervalSeconds: 3600, mpvExecutable: '', qbWebUiUrl: '',
  qbDownloadConfigured: false, qbDownloadDirectory: '',
  qbDownloadEnvironmentOverride: false,
}

afterEach(() => { cleanup(); vi.restoreAllMocks() })
beforeEach(() => {
  vi.spyOn(client, 'getQbConfig').mockResolvedValue({ url: 'http://[::1]:8080', username: '', source: 'none', configured: false })
  vi.spyOn(client, 'getBangumiConfig').mockResolvedValue({ userAgent: '', source: 'none', configured: false })
  vi.stubGlobal('matchMedia', vi.fn().mockImplementation(() => ({ matches: false, addListener: vi.fn(), removeListener: vi.fn(), addEventListener: vi.fn(), removeEventListener: vi.fn() })))
  const computedStyle = window.getComputedStyle.bind(window)
  vi.spyOn(window, 'getComputedStyle').mockImplementation((element) => computedStyle(element))
})

it('tests and saves a Bangumi User-Agent containing the project homepage', async () => {
  vi.spyOn(client, 'getSettings').mockResolvedValue(settings)
  vi.spyOn(client, 'getAuditLogs').mockResolvedValue({ items: [], nextOffset: null })
  const test = vi.spyOn(client, 'testBangumiConfig').mockResolvedValue({ connected: true, errorCode: '' })
  const save = vi.spyOn(client, 'putBangumiConfig').mockResolvedValue({ userAgent: 'suoyi127/Link-to-Bangumi/0.1 (Windows) (https://github.com/suoyi127/Link-to-Bangumi)', source: 'saved', configured: true })
  render(<SettingsPage />)
  const agent = await screen.findByLabelText('Bangumi User-Agent')
  expect((agent as HTMLInputElement).value).toContain('https://github.com/suoyi127/Link-to-Bangumi')
  fireEvent.click(screen.getByRole('button', { name: '测试 Bangumi 连接' }))
  await waitFor(() => expect(test).toHaveBeenCalledWith(expect.stringContaining('suoyi127/Link-to-Bangumi')))
  fireEvent.click(screen.getByRole('button', { name: '保存 Bangumi 配置' }))
  await waitFor(() => expect(save).toHaveBeenCalledOnce())
  expect(await screen.findByText('已保存 Bangumi 配置')).toBeInTheDocument()
})

it('clears saved Bangumi configuration only after confirmation', async () => {
  vi.spyOn(client, 'getSettings').mockResolvedValue({ ...settings, bangumiConfigured: true })
  vi.spyOn(client, 'getAuditLogs').mockResolvedValue({ items: [], nextOffset: null })
  vi.spyOn(client, 'getBangumiConfig').mockResolvedValue({ userAgent: 'saved/App/1.0', source: 'saved', configured: true })
  const clear = vi.spyOn(client, 'deleteBangumiConfig').mockResolvedValue({ userAgent: '', source: 'none', configured: false })
  render(<SettingsPage />)
  fireEvent.click(await screen.findByRole('button', { name: '清除 Bangumi 配置' }))
  expect(clear).not.toHaveBeenCalled()
  fireEvent.click(screen.getByRole('button', { name: '确认清除' }))
  await waitFor(() => expect(clear).toHaveBeenCalledOnce())
  expect(await screen.findByText(/Bangumi 未配置；本地浏览仍可用/)).toBeInTheDocument()
})

it('configures qB Web UI on the page without revealing a saved password', async () => {
  vi.spyOn(client, 'getSettings').mockResolvedValue(settings)
  vi.spyOn(client, 'getAuditLogs').mockResolvedValue({ items: [], nextOffset: null })
  vi.spyOn(client, 'getQbConfig').mockResolvedValue({ url: 'http://[::1]:8080', username: 'suyee', source: 'saved', configured: true })
  vi.spyOn(client, 'getQbStatus').mockResolvedValue({ configured: true, connected: true, errorCode: '', version: '5.2', torrentCount: 1, completedCount: 1 })
  vi.spyOn(client, 'getMikanFeeds').mockResolvedValue({ feedCount: 0, articleCount: 0, pairCount: 0, errorCode: '', feeds: [] })
  const test = vi.spyOn(client, 'testQbConfig').mockResolvedValue({ configured: true, connected: true, errorCode: '', version: '5.2', torrentCount: 1, completedCount: 1 })
  const save = vi.spyOn(client, 'putQbConfig').mockResolvedValue({ url: 'http://[::1]:8080', username: 'suyee', source: 'saved', configured: true })
  render(<SettingsPage />)
  expect(await screen.findByLabelText('qB Web UI 用户名')).toHaveValue('suyee')
  expect(screen.getByLabelText('qB Web UI 密码')).toHaveValue('')
  expect(screen.getByLabelText('qB Web UI 密码')).toHaveAttribute('type', 'password')
  fireEvent.click(screen.getByRole('button', { name: '测试 qB 连接' }))
  await waitFor(() => expect(test).toHaveBeenCalledWith({ url: 'http://[::1]:8080', username: 'suyee', password: '' }))
  expect(await screen.findByText(/测试成功.*5.2/)).toBeInTheDocument()
  fireEvent.click(screen.getByRole('button', { name: '保存 qB 连接' }))
  await waitFor(() => expect(save).toHaveBeenCalledWith({ url: 'http://[::1]:8080', username: 'suyee', password: '' }))
  expect(screen.queryByText('stored-secret')).not.toBeInTheDocument()
})

it('clears saved qB Web UI configuration only after confirmation', async () => {
  vi.spyOn(client, 'getSettings').mockResolvedValue(settings)
  vi.spyOn(client, 'getAuditLogs').mockResolvedValue({ items: [], nextOffset: null })
  vi.spyOn(client, 'getQbConfig').mockResolvedValue({ url: 'http://[::1]:8080', username: 'suyee', source: 'saved', configured: true })
  const clear = vi.spyOn(client, 'deleteQbConfig').mockResolvedValue({ url: 'http://[::1]:8080', username: '', source: 'none', configured: false })
  render(<SettingsPage />)
  fireEvent.click(await screen.findByRole('button', { name: '清除 qB 配置' }))
  expect(clear).not.toHaveBeenCalled()
  fireEvent.click(screen.getByRole('button', { name: '确认清除' }))
  await waitFor(() => expect(clear).toHaveBeenCalledOnce())
  expect(await screen.findByText(/尚未配置 qB Web UI/)).toBeInTheDocument()
})

it('shows immutable effective paths and honest inactive integration status, then saves only preferences', async () => {
  vi.spyOn(client, 'getSettings').mockResolvedValue(settings)
  vi.spyOn(client, 'getAuditLogs').mockResolvedValue({ items: [], nextOffset: null })
  const put = vi.spyOn(client, 'putSettings').mockResolvedValue({ ...settings, preferredOperation: 'copy' })
  render(<SettingsPage />)
  expect(await screen.findByText('尚未配置 qB 下载目录')).toBeInTheDocument()
  expect(screen.getByText('X:/imports')).toBeInTheDocument()
  expect(screen.getByText(/Bangumi 未配置/)).toBeInTheDocument()
  expect(screen.getByText(/qBittorrent Web UI 未启用/)).toBeInTheDocument()
  expect(screen.getByText(/尚未配置 qB 下载目录/)).toBeInTheDocument()
  expect(screen.getByText(/mpv.*本机播放/)).toBeInTheDocument()
  expect(screen.getByText(/扫描间隔.*预留/)).toBeInTheDocument()
  fireEvent.change(screen.getByLabelText('首选整理方式'), { target: { value: 'copy' } })
  fireEvent.click(screen.getByRole('button', { name: '保存偏好' }))
  await waitFor(() => expect(put).toHaveBeenCalledWith({ preferredOperation: 'copy', scanIntervalSeconds: 3600, mpvExecutable: '', qbWebUiUrl: '' }))
})

it('saves a user-selected qB download directory and asks for a restart', async () => {
  vi.spyOn(client, 'getSettings').mockResolvedValue(settings)
  vi.spyOn(client, 'getAuditLogs').mockResolvedValue({ items: [], nextOffset: null })
  const save = vi.spyOn(client, 'putQbDownloadDirectory').mockResolvedValue({
    ...settings, qbDownloadDirectory: 'X:/chosen', restartRequired: true,
  })
  render(<SettingsPage />)
  fireEvent.change(await screen.findByLabelText('qB 下载目录'), { target: { value: 'X:/chosen' } })
  fireEvent.click(screen.getByRole('button', { name: '保存 qB 下载目录' }))
  await waitFor(() => expect(save).toHaveBeenCalledWith('X:/chosen'))
  expect(await screen.findByText(/重启后端后生效/)).toBeInTheDocument()
})

it('keeps Mikan download actions disabled while a saved directory awaits restart', async () => {
  vi.spyOn(client, 'getSettings').mockResolvedValue({
    ...settings, qbWebUiConfigured: true, qbDownloadConfigured: true,
    qbDownloadDirectory: 'X:/new-source',
  })
  vi.spyOn(client, 'getAuditLogs').mockResolvedValue({ items: [], nextOffset: null })
  vi.spyOn(client, 'getQbStatus').mockResolvedValue({ configured: true, connected: true, errorCode: '', version: '5.2', torrentCount: 0, completedCount: 0 })
  vi.spyOn(client, 'getMikanFeeds').mockResolvedValue({ feedCount: 0, articleCount: 0, pairCount: 0, errorCode: '', feeds: [] })
  render(<SettingsPage />)
  expect(await screen.findByText(/重启后端后生效/)).toBeInTheDocument()
  fireEvent.change(screen.getByLabelText('Mikan RSS 地址'), { target: { value: 'https://mikanani.me/RSS/Bangumi?bangumiId=1' } })
  expect(screen.getByRole('button', { name: '添加 Mikan 订阅' })).toBeDisabled()
})

it('keeps an explicit environment source active despite an older saved directory', async () => {
  const overriddenSettings = {
    ...settings, qbWebUiConfigured: true, qbDownloadConfigured: true,
    qbDownloadDirectory: 'X:/older-saved', qbDownloadEnvironmentOverride: true,
  }
  vi.spyOn(client, 'getSettings').mockResolvedValue(overriddenSettings)
  vi.spyOn(client, 'getAuditLogs').mockResolvedValue({ items: [], nextOffset: null })
  vi.spyOn(client, 'getQbStatus').mockResolvedValue({ configured: true, connected: true, errorCode: '', version: '5.2', torrentCount: 0, completedCount: 0 })
  vi.spyOn(client, 'getMikanFeeds').mockResolvedValue({ feedCount: 0, articleCount: 0, pairCount: 0, errorCode: '', feeds: [] })
  render(<SettingsPage />)
  fireEvent.change(await screen.findByLabelText('Mikan RSS 地址'), { target: { value: 'https://mikanani.me/RSS/Bangumi?bangumiId=1' } })
  expect(screen.getByRole('button', { name: '添加 Mikan 订阅' })).toBeEnabled()
  expect(screen.getByRole('button', { name: '保存 qB 下载目录' })).toBeDisabled()
  expect(screen.getByText(/环境变量正在覆盖/)).toBeInTheDocument()
})

it('paginates audit history using the returned cursor', async () => {
  vi.spyOn(client, 'getSettings').mockResolvedValue(settings)
  const audit = vi.spyOn(client, 'getAuditLogs')
    .mockResolvedValueOnce({ items: [{ id: 1, action: 'scanned', entityType: 'scan', entityId: '1', createdAt: 'today' }], nextOffset: 50 })
    .mockResolvedValueOnce({ items: [{ id: 2, action: 'organized', entityType: 'media', entityId: '2', createdAt: 'later' }], nextOffset: null })
  render(<SettingsPage />)
  expect(await screen.findByText(/scanned/)).toBeInTheDocument()
  fireEvent.click(screen.getByRole('button', { name: '下一页审计' }))
  await waitFor(() => expect(audit).toHaveBeenLastCalledWith(50, 50))
  expect(await screen.findByText(/organized/)).toBeInTheDocument()
})

it('shows live qB diagnostics when credentials are configured', async () => {
  vi.spyOn(client, 'getSettings').mockResolvedValue({ ...settings, qbWebUiConfigured: true, qbDownloadConfigured: true })
  vi.spyOn(client, 'getAuditLogs').mockResolvedValue({ items: [], nextOffset: null })
  vi.spyOn(client, 'getQbStatus').mockResolvedValue({ configured: true, connected: true, errorCode: '', version: '5.1.0', torrentCount: 3, completedCount: 2 })
  render(<SettingsPage />)
  expect(await screen.findByText(/qBittorrent 已连接/)).toBeInTheDocument()
  expect(screen.getByText(/3 个任务/)).toBeInTheDocument()
})

it('shows Mikan feeds and adds a user-approved feed through qB', async () => {
  vi.spyOn(client, 'getSettings').mockResolvedValue({ ...settings, qbWebUiConfigured: true, qbDownloadConfigured: true })
  vi.spyOn(client, 'getAuditLogs').mockResolvedValue({ items: [], nextOffset: null })
  vi.spyOn(client, 'getQbStatus').mockResolvedValue({ configured: true, connected: true, errorCode: '', version: '5.2.1', torrentCount: 1, completedCount: 1 })
  const feeds = vi.spyOn(client, 'getMikanFeeds').mockResolvedValue({ feedCount: 1, articleCount: 26, pairCount: 1, errorCode: '', feeds: [{ title: 'Mikan Project', articleCount: 26, hasError: false }] })
  const add = vi.spyOn(client, 'addMikanFeed').mockResolvedValue({ success: true, errorCode: '' })
  render(<SettingsPage />)
  expect(await screen.findByText(/Mikan Project.*26/)).toBeInTheDocument()
  fireEvent.change(screen.getByLabelText('Mikan RSS 地址'), { target: { value: 'https://mikanani.me/RSS/Bangumi?bangumiId=123' } })
  fireEvent.click(screen.getByRole('button', { name: '添加 Mikan 订阅' }))
  await waitFor(() => expect(add).toHaveBeenCalledWith('https://mikanani.me/RSS/Bangumi?bangumiId=123'))
  await waitFor(() => expect(feeds).toHaveBeenCalledTimes(2))
})

it('creates a qB download rule only after confirming its target and immediate effect', async () => {
  vi.spyOn(client, 'getSettings').mockResolvedValue({ ...settings, qbWebUiConfigured: true, qbDownloadConfigured: true })
  vi.spyOn(client, 'getAuditLogs').mockResolvedValue({ items: [], nextOffset: null })
  vi.spyOn(client, 'getQbStatus').mockResolvedValue({ configured: true, connected: true, errorCode: '', version: '5.2.1', torrentCount: 1, completedCount: 1 })
  vi.spyOn(client, 'getMikanFeeds').mockResolvedValue({ feedCount: 1, articleCount: 0, pairCount: 0, errorCode: '', feeds: [] })
  const create = vi.spyOn(client, 'createMikanRule').mockResolvedValue({ success: true, errorCode: '' })
  render(<SettingsPage />)
  expect(await screen.findByText('X:/source')).toBeInTheDocument()
  fireEvent.change(screen.getByLabelText('规则名称'), { target: { value: '新番' } })
  fireEvent.change(screen.getByLabelText('规则订阅地址'), { target: { value: 'https://mikanani.me/RSS/Bangumi?bangumiId=123' } })
  fireEvent.change(screen.getByLabelText('标题包含关键词'), { target: { value: '字幕组' } })
  fireEvent.click(screen.getByRole('button', { name: '创建自动下载规则' }))
  expect(create).not.toHaveBeenCalled()
  fireEvent.click(screen.getByRole('button', { name: '确认创建' }))
  await waitFor(() => expect(create).toHaveBeenCalledWith({ ruleName: '新番', feedUrl: 'https://mikanani.me/RSS/Bangumi?bangumiId=123', keyword: '字幕组' }))
})
