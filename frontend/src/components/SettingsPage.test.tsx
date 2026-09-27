import { cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react'
import { afterEach, beforeEach, expect, it, vi } from 'vitest'
import * as client from '../api/client'
import type { Settings } from '../api/types'
import { SettingsPage } from './SettingsPage'

const settings: Settings = {
  sourcePath: 'X:/source', importPath: 'X:/imports', libraryPath: 'X:/library', dataPath: 'X:/data',
  bangumiConfigured: false, qbWebUiConfigured: false, preferredOperation: 'hardlink',
  scanIntervalSeconds: 3600, mpvExecutable: '', qbWebUiUrl: '',
}

afterEach(() => { cleanup(); vi.restoreAllMocks() })
beforeEach(() => {
  vi.stubGlobal('matchMedia', vi.fn().mockImplementation(() => ({ matches: false, addListener: vi.fn(), removeListener: vi.fn(), addEventListener: vi.fn(), removeEventListener: vi.fn() })))
  const computedStyle = window.getComputedStyle.bind(window)
  vi.spyOn(window, 'getComputedStyle').mockImplementation((element) => computedStyle(element))
})

it('shows immutable effective paths and honest inactive integration status, then saves only preferences', async () => {
  vi.spyOn(client, 'getSettings').mockResolvedValue(settings)
  vi.spyOn(client, 'getAuditLogs').mockResolvedValue({ items: [], nextOffset: null })
  const put = vi.spyOn(client, 'putSettings').mockResolvedValue({ ...settings, preferredOperation: 'copy' })
  render(<SettingsPage />)
  expect(await screen.findByText('X:/source')).toBeInTheDocument()
  expect(screen.getByText('X:/imports')).toBeInTheDocument()
  expect(screen.getByText(/Bangumi 未配置/)).toBeInTheDocument()
  expect(screen.getByText(/qBittorrent Web UI 未启用/)).toBeInTheDocument()
  expect(screen.getByText(/mpv.*本机播放/)).toBeInTheDocument()
  expect(screen.getByText(/扫描间隔.*预留/)).toBeInTheDocument()
  fireEvent.change(screen.getByLabelText('首选整理方式'), { target: { value: 'copy' } })
  fireEvent.click(screen.getByRole('button', { name: '保存偏好' }))
  await waitFor(() => expect(put).toHaveBeenCalledWith({ preferredOperation: 'copy', scanIntervalSeconds: 3600, mpvExecutable: '', qbWebUiUrl: '' }))
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
  vi.spyOn(client, 'getSettings').mockResolvedValue({ ...settings, qbWebUiConfigured: true })
  vi.spyOn(client, 'getAuditLogs').mockResolvedValue({ items: [], nextOffset: null })
  vi.spyOn(client, 'getQbStatus').mockResolvedValue({ configured: true, connected: true, errorCode: '', version: '5.1.0', torrentCount: 3, completedCount: 2 })
  render(<SettingsPage />)
  expect(await screen.findByText(/qBittorrent 已连接/)).toBeInTheDocument()
  expect(screen.getByText(/3 个任务/)).toBeInTheDocument()
})

it('shows Mikan feeds and adds a user-approved feed through qB', async () => {
  vi.spyOn(client, 'getSettings').mockResolvedValue({ ...settings, qbWebUiConfigured: true })
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
  vi.spyOn(client, 'getSettings').mockResolvedValue({ ...settings, qbWebUiConfigured: true })
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
