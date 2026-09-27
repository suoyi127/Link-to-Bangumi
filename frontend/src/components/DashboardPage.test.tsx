import { act, cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react'
import { afterEach, expect, it, vi } from 'vitest'
import * as client from '../api/client'
import * as health from '../api/health'
import { DashboardPage } from './DashboardPage'

afterEach(() => { cleanup(); vi.restoreAllMocks() })

function prepareAntd() {
  vi.stubGlobal('matchMedia', vi.fn().mockImplementation(() => ({ matches: false, addListener: vi.fn(), removeListener: vi.fn(), addEventListener: vi.fn(), removeEventListener: vi.fn() })))
  const getComputedStyle = window.getComputedStyle.bind(window)
  vi.spyOn(window, 'getComputedStyle').mockImplementation((element) => getComputedStyle(element))
}

it('shows counts and refreshes after each distinct scan action', async () => {
  prepareAntd()
  vi.spyOn(health, 'getHealth').mockResolvedValue({ status: 'ok', service: 'anime-vault' })
  vi.spyOn(client, 'getInbox').mockResolvedValue({ items: [{ id: 1 }], total: 1, nextOffset: null } as never)
  vi.spyOn(client, 'getAnime').mockResolvedValue({ items: [{ id: 2 }], nextOffset: null } as never)
  const scans = vi.spyOn(client, 'getScans').mockResolvedValue({ items: [], nextOffset: null })
  const source = vi.spyOn(client, 'startSourceScan').mockResolvedValue({ id: 3 } as never)
  const imports = vi.spyOn(client, 'startImportScan').mockResolvedValue({ id: 4 } as never)
  render(<DashboardPage />)
  expect(screen.queryByText('暂无扫描记录')).not.toBeInTheDocument()
  expect(await screen.findByText('服务正常')).toBeInTheDocument()
  await waitFor(() => expect(scans).toHaveBeenCalledOnce())
  expect(screen.getByText('待整理文件')).toBeInTheDocument()
  fireEvent.click(screen.getByRole('button', { name: '扫描 qB 来源' }))
  await waitFor(() => expect(source).toHaveBeenCalledOnce())
  await waitFor(() => expect(scans).toHaveBeenCalledTimes(2))
  fireEvent.click(screen.getByRole('button', { name: '扫描外部导入' }))
  await waitFor(() => expect(imports).toHaveBeenCalledOnce())
  await waitFor(() => expect(scans).toHaveBeenCalledTimes(3))
})

it('keeps newer scan counts when an older refresh finishes last', async () => {
  prepareAntd()
  vi.spyOn(health, 'getHealth').mockResolvedValue({ status: 'ok', service: 'anime-vault' })
  let resolveOld!: (value: Awaited<ReturnType<typeof client.getInbox>>) => void
  const oldInbox = new Promise<Awaited<ReturnType<typeof client.getInbox>>>((resolve) => { resolveOld = resolve })
  vi.spyOn(client, 'getInbox').mockImplementationOnce(() => oldInbox).mockResolvedValue({ items: [{ id: 1 }], total: 2, nextOffset: 1 } as never)
  vi.spyOn(client, 'getAnime').mockResolvedValue({ items: [], nextOffset: null })
  vi.spyOn(client, 'getScans').mockResolvedValue({ items: [], nextOffset: null })
  vi.spyOn(client, 'startSourceScan').mockResolvedValue({ id: 1 } as never)
  render(<DashboardPage />)
  fireEvent.click(screen.getByRole('button', { name: '扫描 qB 来源' }))
  await waitFor(() => expect(screen.getByText('待整理文件').closest('.ant-card')).toHaveTextContent('2'))
  await act(async () => { resolveOld({ items: [{ id: 1 }], total: 1, nextOffset: null } as never) })
  expect(screen.getByText('待整理文件').closest('.ant-card')).toHaveTextContent('2')
})

it('shows an initial load error without empty scan messages', async () => {
  prepareAntd()
  vi.spyOn(health, 'getHealth').mockResolvedValue({ status: 'ok', service: 'anime-vault' })
  vi.spyOn(client, 'getInbox').mockRejectedValue(new Error('加载失败'))
  vi.spyOn(client, 'getAnime').mockResolvedValue({ items: [], nextOffset: null })
  vi.spyOn(client, 'getScans').mockResolvedValue({ items: [], nextOffset: null })
  render(<DashboardPage />)
  expect(await screen.findByText('加载失败')).toBeInTheDocument()
  expect(screen.queryByText('暂无扫描记录')).not.toBeInTheDocument()
  expect(screen.queryByText('暂无失败记录')).not.toBeInTheDocument()
})

it('runs a manual Mikan and Bangumi metadata sync for existing library entries', async () => {
  prepareAntd()
  vi.spyOn(health, 'getHealth').mockResolvedValue({ status: 'ok', service: 'anime-vault' })
  vi.spyOn(client, 'getInbox').mockResolvedValue({ items: [], total: 0, nextOffset: null })
  vi.spyOn(client, 'getAnime').mockResolvedValue({ items: [], nextOffset: null })
  vi.spyOn(client, 'getScans').mockResolvedValue({ items: [], nextOffset: null })
  const sync = vi.spyOn(client, 'syncMetadata').mockResolvedValue({ mikanApplied: 1, bangumiBound: 0, mikanErrorCode: '' })
  render(<DashboardPage />)
  fireEvent.click(await screen.findByRole('button', { name: '同步刮削元数据' }))
  await waitFor(() => expect(sync).toHaveBeenCalledOnce())
  expect(await screen.findByText(/Mikan 更新 1 部/)).toBeInTheDocument()
})
