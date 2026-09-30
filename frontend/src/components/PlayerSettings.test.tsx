import { cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react'
import { afterEach, expect, it, vi } from 'vitest'
import * as client from '../api/client'
import { PlayerSettings } from './PlayerSettings'

afterEach(() => { cleanup(); vi.restoreAllMocks() })
it('inherits mpv and saves another default with no executable for system association', async () => {
  const items = [{ id: 'mpv', name: 'mpv', executable: 'D:/mpv.exe', available: true }, { id: 'system', name: '系统默认播放器', executable: '', available: true }]
  vi.spyOn(client, 'getPlayers').mockResolvedValue({ selectedId: 'mpv', items })
  const save = vi.spyOn(client, 'savePlayer').mockResolvedValue({ selectedId: 'system', items })
  render(<PlayerSettings />)
  expect(await screen.findByLabelText('播放器可执行文件')).toHaveValue('D:/mpv.exe')
  fireEvent.change(screen.getByLabelText('默认播放器'), { target: { value: 'system' } })
  expect(screen.queryByLabelText('播放器可执行文件')).not.toBeInTheDocument()
  fireEvent.click(screen.getByRole('button', { name: '保存默认播放器' }))
  await waitFor(() => expect(save).toHaveBeenCalledWith('system', ''))
  expect(await screen.findByText('默认播放器已保存，播放时仍可临时切换。')).toBeInTheDocument()
})

it('allows portable exe paths and reports validation failure without claiming success', async () => {
  vi.spyOn(client, 'getPlayers').mockResolvedValue({ selectedId: 'mpv', items: [{ id: 'mpv', name: 'mpv', executable: '', available: false }, { id: 'custom', name: '自定义播放器', executable: '', available: false }] })
  const save = vi.spyOn(client, 'savePlayer').mockRejectedValue(new Error('文件不存在'))
  render(<PlayerSettings />)
  fireEvent.change(await screen.findByLabelText('默认播放器'), { target: { value: 'custom' } })
  fireEvent.change(screen.getByLabelText('播放器可执行文件'), { target: { value: 'D:/portable/player.exe' } })
  fireEvent.click(screen.getByRole('button', { name: '保存默认播放器' }))
  await waitFor(() => expect(save).toHaveBeenCalledWith('custom', 'D:/portable/player.exe'))
  expect(await screen.findByText('文件不存在')).toBeInTheDocument()
  expect(screen.queryByText('默认播放器已保存，播放时仍可临时切换。')).not.toBeInTheDocument()
})
