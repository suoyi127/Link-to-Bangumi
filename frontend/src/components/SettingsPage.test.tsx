import { act, cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react'
import { afterEach, beforeEach, expect, it, vi } from 'vitest'
import * as mediaApi from '../api/media'
import * as qbApi from '../api/qb'
import * as settingsApi from '../api/settings'
import type { Settings } from '../api/types'
import { SettingsPage } from './SettingsPage'

const settings: Settings = {
  sourcePath: 'X:/source', importPath: 'X:/imports', libraryPath: 'X:/library', dataPath: 'X:/data',
  bangumiConfigured: false, qbWebUiConfigured: false, preferredOperation: 'hardlink',
  scanIntervalSeconds: 3600, mpvExecutable: '', qbWebUiUrl: '',
  qbDownloadConfigured: false, qbDownloadDirectory: '',
  qbDownloadEnvironmentOverride: false,
}

afterEach(() => { cleanup(); vi.restoreAllMocks(); vi.unstubAllGlobals() })
beforeEach(() => {
  vi.spyOn(mediaApi, 'getPlayers').mockResolvedValue({ selectedId: 'mpv', items: [{ id: 'mpv', name: 'mpv', executable: '', available: false }, { id: 'system', name: '系统默认播放器', executable: '', available: true }] })
  vi.spyOn(qbApi, 'getQbConfig').mockResolvedValue({ url: 'http://[::1]:8080', username: '', source: 'none', configured: false })
  vi.spyOn(settingsApi, 'getBangumiConfig').mockResolvedValue({ userAgent: '', source: 'none', configured: false })
  vi.stubGlobal('matchMedia', vi.fn().mockImplementation(() => ({ matches: false, addListener: vi.fn(), removeListener: vi.fn(), addEventListener: vi.fn(), removeEventListener: vi.fn() })))
  const computedStyle = window.getComputedStyle.bind(window)
  vi.spyOn(window, 'getComputedStyle').mockImplementation((element) => computedStyle(element))
})

it('tests and saves a Bangumi User-Agent containing the project homepage', async () => {
  vi.spyOn(settingsApi, 'getSettings').mockResolvedValue(settings)
  vi.spyOn(settingsApi, 'getAuditLogs').mockResolvedValue({ items: [], nextOffset: null })
  const test = vi.spyOn(settingsApi, 'testBangumiConfig').mockResolvedValue({ connected: true, errorCode: '' })
  const save = vi.spyOn(settingsApi, 'putBangumiConfig').mockResolvedValue({ userAgent: 'suoyi127/Link-to-Bangumi/0.1 (Windows) (https://github.com/suoyi127/Link-to-Bangumi)', source: 'saved', configured: true })
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
  vi.spyOn(settingsApi, 'getSettings').mockResolvedValue({ ...settings, bangumiConfigured: true })
  vi.spyOn(settingsApi, 'getAuditLogs').mockResolvedValue({ items: [], nextOffset: null })
  vi.spyOn(settingsApi, 'getBangumiConfig').mockResolvedValue({ userAgent: 'saved/App/1.0', source: 'saved', configured: true })
  const clear = vi.spyOn(settingsApi, 'deleteBangumiConfig').mockResolvedValue({ userAgent: '', source: 'none', configured: false })
  render(<SettingsPage />)
  fireEvent.click(await screen.findByRole('button', { name: '清除 Bangumi 配置' }))
  expect(clear).not.toHaveBeenCalled()
  fireEvent.click(screen.getByRole('button', { name: '确认清除' }))
  await waitFor(() => expect(clear).toHaveBeenCalledOnce())
  expect(await screen.findByText(/Bangumi 未配置；本地浏览仍可用/)).toBeInTheDocument()
})

it('configures qB Web UI on the page without revealing a saved password', async () => {
  vi.spyOn(settingsApi, 'getSettings').mockResolvedValue(settings)
  vi.spyOn(settingsApi, 'getAuditLogs').mockResolvedValue({ items: [], nextOffset: null })
  vi.spyOn(qbApi, 'getQbConfig').mockResolvedValue({ url: 'http://[::1]:8080', username: 'suyee', source: 'saved', configured: true })
  vi.spyOn(qbApi, 'getQbStatus').mockResolvedValue({ configured: true, connected: true, errorCode: '', version: '5.2', torrentCount: 1, completedCount: 1 })
  vi.spyOn(qbApi, 'getMikanFeeds').mockResolvedValue({ feedCount: 0, articleCount: 0, pairCount: 0, errorCode: '', feeds: [] })
  const test = vi.spyOn(qbApi, 'testQbConfig').mockResolvedValue({ configured: true, connected: true, errorCode: '', version: '5.2', torrentCount: 1, completedCount: 1 })
  const save = vi.spyOn(qbApi, 'putQbConfig').mockResolvedValue({ url: 'http://[::1]:8080', username: 'suyee', source: 'saved', configured: true })
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
  vi.spyOn(settingsApi, 'getSettings').mockResolvedValue(settings)
  vi.spyOn(settingsApi, 'getAuditLogs').mockResolvedValue({ items: [], nextOffset: null })
  vi.spyOn(qbApi, 'getQbConfig').mockResolvedValue({ url: 'http://[::1]:8080', username: 'suyee', source: 'saved', configured: true })
  const clear = vi.spyOn(qbApi, 'deleteQbConfig').mockResolvedValue({ url: 'http://[::1]:8080', username: '', source: 'none', configured: false })
  render(<SettingsPage />)
  fireEvent.click(await screen.findByRole('button', { name: '清除 qB 配置' }))
  expect(clear).not.toHaveBeenCalled()
  fireEvent.click(screen.getByRole('button', { name: '确认清除' }))
  await waitFor(() => expect(clear).toHaveBeenCalledOnce())
  expect(await screen.findByText(/尚未配置 qB Web UI/)).toBeInTheDocument()
})

it('shows immutable effective paths and honest inactive integration status, then saves only preferences', async () => {
  vi.spyOn(settingsApi, 'getSettings').mockResolvedValue(settings)
  vi.spyOn(settingsApi, 'getAuditLogs').mockResolvedValue({ items: [], nextOffset: null })
  const put = vi.spyOn(settingsApi, 'putSettings').mockResolvedValue({ ...settings, preferredOperation: 'copy' })
  render(<SettingsPage />)
  expect(await screen.findByText('尚未配置 qB 下载目录')).toBeInTheDocument()
  expect(screen.getByText('X:/imports')).toBeInTheDocument()
  expect(screen.getByText(/Bangumi 未配置/)).toBeInTheDocument()
  expect(screen.getByText(/qBittorrent Web UI 未启用/)).toBeInTheDocument()
  expect(screen.getByText(/尚未配置 qB 下载目录/)).toBeInTheDocument()
  expect(screen.getByText('本机播放器')).toBeInTheDocument()
  expect(screen.getByText(/扫描间隔.*预留/)).toBeInTheDocument()
  fireEvent.change(screen.getByLabelText('首选整理方式'), { target: { value: 'copy' } })
  fireEvent.click(screen.getByRole('button', { name: '保存偏好' }))
  await waitFor(() => expect(put).toHaveBeenCalledWith({ preferredOperation: 'copy', scanIntervalSeconds: 3600, mpvExecutable: '', qbWebUiUrl: '' }))
})

it('saves a user-selected qB download directory and asks for a restart', async () => {
  vi.spyOn(settingsApi, 'getSettings').mockResolvedValue(settings)
  vi.spyOn(settingsApi, 'getAuditLogs').mockResolvedValue({ items: [], nextOffset: null })
  const save = vi.spyOn(settingsApi, 'putQbDownloadDirectory').mockResolvedValue({
    ...settings, qbDownloadDirectory: 'X:/chosen', restartRequired: true,
  })
  render(<SettingsPage />)
  fireEvent.change(await screen.findByLabelText('qB 下载目录'), { target: { value: 'X:/chosen' } })
  fireEvent.click(screen.getByRole('button', { name: '保存 qB 下载目录' }))
  await waitFor(() => expect(save).toHaveBeenCalledWith('X:/chosen'))
  expect(await screen.findByText(/重启后端后生效/)).toBeInTheDocument()
})

it('requests desktop restart after saving the qB directory successfully', async () => {
  const postMessage = vi.fn()
  vi.stubGlobal('chrome', { webview: { postMessage } })
  vi.spyOn(settingsApi, 'getSettings').mockResolvedValue(settings)
  vi.spyOn(settingsApi, 'getAuditLogs').mockResolvedValue({ items: [], nextOffset: null })
  vi.spyOn(settingsApi, 'putQbDownloadDirectory').mockResolvedValue({ ...settings, qbDownloadDirectory: 'X:/chosen', restartRequired: true })
  render(<SettingsPage />)
  fireEvent.change(await screen.findByLabelText('qB 下载目录'), { target: { value: 'X:/chosen' } })
  fireEvent.click(screen.getByRole('button', { name: '保存 qB 下载目录' }))
  await waitFor(() => expect(postMessage).toHaveBeenCalledExactlyOnceWith({ command: 'restart-backend' }))
  expect(await screen.findByText(/正在自动重启后端/)).toBeInTheDocument()
})

it('does not request desktop restart when saving the qB directory fails', async () => {
  const postMessage = vi.fn()
  vi.stubGlobal('chrome', { webview: { postMessage } })
  vi.spyOn(settingsApi, 'getSettings').mockResolvedValue(settings)
  vi.spyOn(settingsApi, 'getAuditLogs').mockResolvedValue({ items: [], nextOffset: null })
  vi.spyOn(settingsApi, 'putQbDownloadDirectory').mockRejectedValue(new Error('目录无效'))
  render(<SettingsPage />)
  fireEvent.change(await screen.findByLabelText('qB 下载目录'), { target: { value: 'X:/missing' } })
  fireEvent.click(screen.getByRole('button', { name: '保存 qB 下载目录' }))
  expect(await screen.findByText('目录无效')).toBeInTheDocument()
  expect(postMessage).not.toHaveBeenCalled()
})

it('keeps Mikan download actions disabled while a saved directory awaits restart', async () => {
  vi.spyOn(settingsApi, 'getSettings').mockResolvedValue({
    ...settings, qbWebUiConfigured: true, qbDownloadConfigured: true,
    qbDownloadDirectory: 'X:/new-source',
  })
  vi.spyOn(settingsApi, 'getAuditLogs').mockResolvedValue({ items: [], nextOffset: null })
  vi.spyOn(qbApi, 'getQbStatus').mockResolvedValue({ configured: true, connected: true, errorCode: '', version: '5.2', torrentCount: 0, completedCount: 0 })
  vi.spyOn(qbApi, 'getMikanFeeds').mockResolvedValue({ feedCount: 0, articleCount: 0, pairCount: 0, errorCode: '', feeds: [] })
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
  vi.spyOn(settingsApi, 'getSettings').mockResolvedValue(overriddenSettings)
  vi.spyOn(settingsApi, 'getAuditLogs').mockResolvedValue({ items: [], nextOffset: null })
  vi.spyOn(qbApi, 'getQbStatus').mockResolvedValue({ configured: true, connected: true, errorCode: '', version: '5.2', torrentCount: 0, completedCount: 0 })
  vi.spyOn(qbApi, 'getMikanFeeds').mockResolvedValue({ feedCount: 0, articleCount: 0, pairCount: 0, errorCode: '', feeds: [] })
  render(<SettingsPage />)
  fireEvent.change(await screen.findByLabelText('Mikan RSS 地址'), { target: { value: 'https://mikanani.me/RSS/Bangumi?bangumiId=1' } })
  expect(screen.getByRole('button', { name: '添加 Mikan 订阅' })).toBeEnabled()
  expect(screen.getByRole('button', { name: '保存 qB 下载目录' })).toBeDisabled()
  expect(screen.getByText(/环境变量正在覆盖/)).toBeInTheDocument()
})

it('paginates audit history using the returned cursor', async () => {
  vi.spyOn(settingsApi, 'getSettings').mockResolvedValue(settings)
  const audit = vi.spyOn(settingsApi, 'getAuditLogs')
    .mockResolvedValueOnce({ items: [{ id: 1, action: 'scanned', entityType: 'scan', entityId: '1', createdAt: 'today' }], nextOffset: 50 })
    .mockResolvedValueOnce({ items: [{ id: 2, action: 'organized', entityType: 'media', entityId: '2', createdAt: 'later' }], nextOffset: null })
  render(<SettingsPage />)
  fireEvent.click(screen.getByText('审计记录'))
  expect(await screen.findByText(/scanned/)).toBeInTheDocument()
  fireEvent.click(screen.getByRole('button', { name: '下一页审计' }))
  await waitFor(() => expect(audit).toHaveBeenLastCalledWith(50, 50, undefined, expect.any(AbortSignal)))
  expect(await screen.findByText(/organized/)).toBeInTheDocument()
})

it('shows live qB diagnostics when credentials are configured', async () => {
  vi.spyOn(settingsApi, 'getSettings').mockResolvedValue({ ...settings, qbWebUiConfigured: true, qbDownloadConfigured: true })
  vi.spyOn(settingsApi, 'getAuditLogs').mockResolvedValue({ items: [], nextOffset: null })
  vi.spyOn(qbApi, 'getQbStatus').mockResolvedValue({ configured: true, connected: true, errorCode: '', version: '5.1.0', torrentCount: 3, completedCount: 2 })
  render(<SettingsPage />)
  expect(await screen.findByText(/qBittorrent 已连接/)).toBeInTheDocument()
  expect(screen.getByText(/3 个任务/)).toBeInTheDocument()
})

it('shows Mikan feeds and adds a user-approved feed through qB', async () => {
  vi.spyOn(settingsApi, 'getSettings').mockResolvedValue({ ...settings, qbWebUiConfigured: true, qbDownloadConfigured: true })
  vi.spyOn(settingsApi, 'getAuditLogs').mockResolvedValue({ items: [], nextOffset: null })
  vi.spyOn(qbApi, 'getQbStatus').mockResolvedValue({ configured: true, connected: true, errorCode: '', version: '5.2.1', torrentCount: 1, completedCount: 1 })
  const feeds = vi.spyOn(qbApi, 'getMikanFeeds').mockResolvedValue({ feedCount: 1, articleCount: 26, pairCount: 1, errorCode: '', feeds: [{ title: 'Mikan Project', articleCount: 26, hasError: false }] })
  const add = vi.spyOn(qbApi, 'addMikanFeed').mockResolvedValue({ success: true, errorCode: '' })
  render(<SettingsPage />)
  await screen.findByText(/1 个订阅 · 26 篇文章/)
  const toggle = screen.getByText('订阅列表与文章统计')
  const details = toggle.closest('details')!
  expect(details.open).toBe(false)
  fireEvent.click(toggle)
  expect(details.open).toBe(true)
  expect(await screen.findByText(/Mikan Project.*26/)).toBeVisible()
  fireEvent.click(toggle)
  expect(details.open).toBe(false)
  fireEvent.change(screen.getByLabelText('Mikan RSS 地址'), { target: { value: 'https://mikanani.me/RSS/Bangumi?bangumiId=123' } })
  fireEvent.click(screen.getByRole('button', { name: '添加 Mikan 订阅' }))
  await waitFor(() => expect(add).toHaveBeenCalledWith('https://mikanani.me/RSS/Bangumi?bangumiId=123'))
  await waitFor(() => expect(feeds).toHaveBeenCalledTimes(2))
})

it('creates a qB download rule only after confirming its target and immediate effect', async () => {
  vi.spyOn(settingsApi, 'getSettings').mockResolvedValue({ ...settings, qbWebUiConfigured: true, qbDownloadConfigured: true })
  vi.spyOn(settingsApi, 'getAuditLogs').mockResolvedValue({ items: [], nextOffset: null })
  vi.spyOn(qbApi, 'getQbStatus').mockResolvedValue({ configured: true, connected: true, errorCode: '', version: '5.2.1', torrentCount: 1, completedCount: 1 })
  vi.spyOn(qbApi, 'getMikanFeeds').mockResolvedValue({ feedCount: 1, articleCount: 0, pairCount: 0, errorCode: '', feeds: [] })
  const create = vi.spyOn(qbApi, 'createMikanRule').mockResolvedValue({ success: true, errorCode: '' })
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

function deferred<T>() {
  let resolve!: (value: T) => void
  const promise = new Promise<T>((done) => { resolve = done })
  return { promise, resolve }
}

const connectedStatus = { configured: true, connected: true, errorCode: '', version: '5.2', torrentCount: 1, completedCount: 1 }
const emptyFeeds = { feedCount: 0, articleCount: 0, pairCount: 0, errorCode: '', feeds: [] }
const savedQb = { url: 'http://[::1]:8080', username: 'saved-user', source: 'saved' as const, configured: true }

it('loads independent settings and audit when a connection configuration fails', async () => {
  vi.mocked(qbApi.getQbConfig).mockRejectedValue(new Error('qB 配置读取失败'))
  vi.mocked(settingsApi.getBangumiConfig).mockResolvedValue({ userAgent: 'available-agent', source: 'saved', configured: true })
  vi.spyOn(settingsApi, 'getSettings').mockResolvedValue(settings)
  vi.spyOn(settingsApi, 'getAuditLogs').mockResolvedValue({ items: [], nextOffset: null })
  render(<SettingsPage />)
  expect(await screen.findByText('qB 配置读取失败')).toBeInTheDocument()
  expect(screen.getByLabelText('Bangumi User-Agent')).toHaveValue('available-agent')
  expect(screen.getByLabelText('首选整理方式')).toHaveValue('hardlink')
  expect(settingsApi.getAuditLogs).toHaveBeenCalledOnce()
})

it.each([false, true])('fetches configuration once and gates diagnostics using settings configured=%s', async (configured) => {
  vi.mocked(qbApi.getQbConfig).mockResolvedValue(savedQb)
  const load = vi.spyOn(settingsApi, 'getSettings').mockResolvedValue({ ...settings, qbWebUiConfigured: configured })
  const audit = vi.spyOn(settingsApi, 'getAuditLogs').mockResolvedValue({ items: [], nextOffset: null })
  const status = vi.spyOn(qbApi, 'getQbStatus').mockResolvedValue(connectedStatus)
  const feeds = vi.spyOn(qbApi, 'getMikanFeeds').mockResolvedValue(emptyFeeds)
  render(<SettingsPage />)
  await screen.findByLabelText('首选整理方式')
  await waitFor(() => expect(audit).toHaveBeenCalledOnce())
  expect(qbApi.getQbConfig).toHaveBeenCalledOnce()
  expect(settingsApi.getBangumiConfig).toHaveBeenCalledOnce()
  expect(load).toHaveBeenCalledOnce()
  expect(status).toHaveBeenCalledTimes(configured ? 1 : 0)
  expect(feeds).toHaveBeenCalledTimes(configured ? 1 : 0)
  expect(vi.mocked(qbApi.getQbConfig).mock.invocationCallOrder[0]).toBeLessThan(vi.mocked(settingsApi.getBangumiConfig).mock.invocationCallOrder[0])
  expect(vi.mocked(settingsApi.getBangumiConfig).mock.invocationCallOrder[0]).toBeLessThan(load.mock.invocationCallOrder[0])
  expect(load.mock.invocationCallOrder[0]).toBeLessThan(audit.mock.invocationCallOrder[0])
})

it('refreshes status and RSS on qB resave but only status after clearing to environment configuration', async () => {
  vi.mocked(qbApi.getQbConfig).mockResolvedValue(savedQb)
  vi.spyOn(settingsApi, 'getSettings').mockResolvedValue({ ...settings, qbWebUiConfigured: true })
  vi.spyOn(settingsApi, 'getAuditLogs').mockResolvedValue({ items: [], nextOffset: null })
  const status = vi.spyOn(qbApi, 'getQbStatus').mockResolvedValue(connectedStatus)
  const feeds = vi.spyOn(qbApi, 'getMikanFeeds').mockResolvedValue(emptyFeeds)
  vi.spyOn(qbApi, 'putQbConfig').mockResolvedValue(savedQb)
  vi.spyOn(qbApi, 'deleteQbConfig').mockResolvedValue({ ...savedQb, source: 'environment' })
  render(<SettingsPage />)
  fireEvent.click(await screen.findByRole('button', { name: '保存 qB 连接' }))
  await waitFor(() => expect(feeds).toHaveBeenCalledTimes(2))
  expect(status).toHaveBeenCalledTimes(2)
  fireEvent.click(await screen.findByRole('button', { name: '清除 qB 配置' }))
  fireEvent.click(screen.getByRole('button', { name: '确认清除' }))
  await waitFor(() => expect(status).toHaveBeenCalledTimes(3))
  expect(feeds).toHaveBeenCalledTimes(2)
  expect(screen.queryByText(/0 个订阅/)).not.toBeInTheDocument()
})

it('does not restore saved qB configuration from a late initial response', async () => {
  const initial = deferred<Awaited<ReturnType<typeof qbApi.getQbConfig>>>()
  vi.mocked(qbApi.getQbConfig).mockReturnValue(initial.promise)
  vi.spyOn(settingsApi, 'getSettings').mockResolvedValue(settings)
  vi.spyOn(settingsApi, 'getAuditLogs').mockResolvedValue({ items: [], nextOffset: null })
  vi.spyOn(qbApi, 'getQbStatus').mockResolvedValue(connectedStatus)
  vi.spyOn(qbApi, 'getMikanFeeds').mockResolvedValue(emptyFeeds)
  vi.spyOn(qbApi, 'putQbConfig').mockResolvedValue(savedQb)
  render(<SettingsPage />)
  fireEvent.change(await screen.findByLabelText('qB Web UI 用户名'), { target: { value: 'saved-user' } })
  fireEvent.change(screen.getByLabelText('qB Web UI 密码'), { target: { value: 'new-secret' } })
  fireEvent.click(screen.getByRole('button', { name: '保存 qB 连接' }))
  await screen.findByText('已保存 qB Web UI 配置')
  await act(async () => initial.resolve({ url: 'http://127.0.0.1:8080', username: 'old-user', source: 'none', configured: false }))
  expect(screen.getByLabelText('qB Web UI 用户名')).toHaveValue('saved-user')
  expect(screen.getByText('已保存 qB Web UI 配置')).toBeInTheDocument()
})

it('invalidates pending diagnostics and RSS when qB configuration is cleared', async () => {
  const oldStatus = deferred<Awaited<ReturnType<typeof qbApi.getQbStatus>>>()
  const oldFeeds = deferred<Awaited<ReturnType<typeof qbApi.getMikanFeeds>>>()
  vi.mocked(qbApi.getQbConfig).mockResolvedValue(savedQb)
  vi.spyOn(settingsApi, 'getSettings').mockResolvedValue({ ...settings, qbWebUiConfigured: true })
  vi.spyOn(settingsApi, 'getAuditLogs').mockResolvedValue({ items: [], nextOffset: null })
  const status = vi.spyOn(qbApi, 'getQbStatus').mockReturnValue(oldStatus.promise)
  const feeds = vi.spyOn(qbApi, 'getMikanFeeds').mockReturnValue(oldFeeds.promise)
  vi.spyOn(qbApi, 'deleteQbConfig').mockResolvedValue({ ...savedQb, source: 'none', configured: false })
  render(<SettingsPage />)
  fireEvent.click(await screen.findByRole('button', { name: '清除 qB 配置' }))
  fireEvent.click(screen.getByRole('button', { name: '确认清除' }))
  await screen.findByText(/qBittorrent Web UI 未启用/)
  await act(async () => {
    oldStatus.resolve(connectedStatus)
    oldFeeds.resolve({ ...emptyFeeds, feedCount: 9 })
  })
  expect(screen.queryByText(/qBittorrent 已连接/)).not.toBeInTheDocument()
  expect(screen.queryByText(/9 个订阅/)).not.toBeInTheDocument()
  expect(status.mock.calls[0][0]?.aborted).toBe(true)
  expect(feeds.mock.calls[0][0]?.aborted).toBe(true)
})

it('keeps unrelated preference and directory drafts when saving the other section', async () => {
  vi.spyOn(settingsApi, 'getSettings').mockResolvedValue(settings)
  vi.spyOn(settingsApi, 'getAuditLogs').mockResolvedValue({ items: [], nextOffset: null })
  vi.spyOn(settingsApi, 'putQbDownloadDirectory').mockResolvedValue({ ...settings, qbDownloadDirectory: 'X:/saved', restartRequired: true })
  vi.spyOn(settingsApi, 'putSettings').mockResolvedValue({ ...settings, preferredOperation: 'copy' })
  render(<SettingsPage />)
  fireEvent.change(await screen.findByLabelText('首选整理方式'), { target: { value: 'copy' } })
  fireEvent.change(screen.getByLabelText('qB 下载目录'), { target: { value: 'X:/saved' } })
  fireEvent.click(screen.getByRole('button', { name: '保存 qB 下载目录' }))
  await screen.findByText(/重启后端后生效/)
  expect(screen.getByLabelText('首选整理方式')).toHaveValue('copy')
  fireEvent.change(screen.getByLabelText('qB 下载目录'), { target: { value: 'X:/unsaved' } })
  fireEvent.click(screen.getByRole('button', { name: '保存偏好' }))
  await waitFor(() => expect(settingsApi.putSettings).toHaveBeenCalledOnce())
  expect(screen.getByLabelText('qB 下载目录')).toHaveValue('X:/unsaved')
})

it('cancels audit reads independently after paging and unmount', async () => {
  vi.spyOn(settingsApi, 'getSettings').mockResolvedValue(settings)
  const late = deferred<Awaited<ReturnType<typeof settingsApi.getAuditLogs>>>()
  const audit = vi.spyOn(settingsApi, 'getAuditLogs')
    .mockResolvedValueOnce({ items: [], nextOffset: 70 }).mockReturnValueOnce(late.promise)
  const view = render(<SettingsPage />)
  fireEvent.click(screen.getByText('审计记录'))
  await waitFor(() => expect(screen.getByRole('button', { name: '下一页审计' })).toBeEnabled())
  fireEvent.click(screen.getByRole('button', { name: '下一页审计' }))
  await waitFor(() => expect(audit).toHaveBeenCalledTimes(2))
  expect(audit.mock.calls[1][1]).toBe(70)
  expect(audit.mock.calls[0][3]?.aborted).toBe(true)
  view.unmount()
  expect(audit.mock.calls[1][3]?.aborted).toBe(true)
  await act(async () => late.resolve({ items: [{ id: 99, action: 'stale-audit', entityType: 'scan', entityId: '99', createdAt: 'old' }], nextOffset: null }))
  expect(screen.queryByText(/stale-audit/)).not.toBeInTheDocument()
})

it('refreshes RSS after a partially failed add and retains the action warning', async () => {
  vi.spyOn(settingsApi, 'getSettings').mockResolvedValue({ ...settings, qbWebUiConfigured: true, qbDownloadConfigured: true })
  vi.spyOn(settingsApi, 'getAuditLogs').mockResolvedValue({ items: [], nextOffset: null })
  vi.spyOn(qbApi, 'getQbStatus').mockResolvedValue(connectedStatus)
  const feeds = vi.spyOn(qbApi, 'getMikanFeeds').mockResolvedValueOnce(emptyFeeds).mockResolvedValue({ ...emptyFeeds, feedCount: 1 })
  vi.spyOn(qbApi, 'addMikanFeed').mockResolvedValue({ success: false, errorCode: 'rss_rule_failed' })
  render(<SettingsPage />)
  fireEvent.change(await screen.findByLabelText('Mikan RSS 地址'), { target: { value: 'https://mikanani.me/RSS/Bangumi?bangumiId=1' } })
  fireEvent.click(screen.getByRole('button', { name: '添加 Mikan 订阅' }))
  expect(await screen.findByText(/rss_rule_failed；若订阅已出现/)).toBeInTheDocument()
  await waitFor(() => expect(feeds).toHaveBeenCalledTimes(2))
  expect(screen.getByText(/1 个订阅/)).toBeInTheDocument()
  expect(screen.getByText(/rss_rule_failed；若订阅已出现/)).toBeInTheDocument()
})

it('retries RSS after a successful add cannot refresh and keeps the action warning', async () => {
  vi.spyOn(settingsApi, 'getSettings').mockResolvedValue({ ...settings, qbWebUiConfigured: true, qbDownloadConfigured: true })
  vi.spyOn(settingsApi, 'getAuditLogs').mockResolvedValue({ items: [], nextOffset: null })
  vi.spyOn(qbApi, 'getQbStatus').mockResolvedValue(connectedStatus)
  const feeds = vi.spyOn(qbApi, 'getMikanFeeds').mockResolvedValueOnce(emptyFeeds)
    .mockRejectedValueOnce(new Error('RSS 读取失败')).mockResolvedValue({ ...emptyFeeds, feedCount: 1 })
  vi.spyOn(qbApi, 'addMikanFeed').mockResolvedValue({ success: true, errorCode: '' })
  render(<SettingsPage />)
  fireEvent.change(await screen.findByLabelText('Mikan RSS 地址'), { target: { value: 'https://mikanani.me/RSS/Bangumi?bangumiId=1' } })
  fireEvent.click(screen.getByRole('button', { name: '添加 Mikan 订阅' }))
  expect(await screen.findByText(/RSS 读取失败；若订阅已出现/)).toBeInTheDocument()
  await waitFor(() => expect(feeds).toHaveBeenCalledTimes(3))
  expect(screen.getByText(/1 个订阅/)).toBeInTheDocument()
  expect(screen.getByText(/RSS 读取失败；若订阅已出现/)).toBeInTheDocument()
})

it('clears a previous saved-connection status error when testing qB again', async () => {
  vi.mocked(qbApi.getQbConfig).mockResolvedValue(savedQb)
  vi.spyOn(settingsApi, 'getSettings').mockResolvedValue(settings)
  vi.spyOn(settingsApi, 'getAuditLogs').mockResolvedValue({ items: [], nextOffset: null })
  vi.spyOn(qbApi, 'getQbStatus').mockRejectedValue(new Error('旧连接状态错误'))
  vi.spyOn(qbApi, 'getMikanFeeds').mockResolvedValue(emptyFeeds)
  vi.spyOn(qbApi, 'putQbConfig').mockResolvedValue(savedQb)
  vi.spyOn(qbApi, 'testQbConfig').mockResolvedValue(connectedStatus)
  render(<SettingsPage />)
  fireEvent.click(await screen.findByRole('button', { name: '保存 qB 连接' }))
  expect(await screen.findByText('旧连接状态错误')).toBeInTheDocument()
  fireEvent.click(screen.getByRole('button', { name: '测试 qB 连接' }))
  await screen.findByText(/测试成功.*5.2/)
  expect(screen.queryByText('旧连接状态错误')).not.toBeInTheDocument()
})
