import { cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react'
import { afterEach, beforeEach, expect, it, vi } from 'vitest'
import * as client from '../api/client'
import type { Anime } from '../api/types'
import { LibraryPage } from './LibraryPage'

afterEach(() => { cleanup(); vi.restoreAllMocks() })
beforeEach(() => {
  vi.stubGlobal('matchMedia', vi.fn().mockImplementation(() => ({ matches: false, addListener: vi.fn(), removeListener: vi.fn(), addEventListener: vi.fn(), removeEventListener: vi.fn() })))
  const computedStyle = window.getComputedStyle.bind(window)
  vi.spyOn(window, 'getComputedStyle').mockImplementation((element) => computedStyle(element))
})
const anime = (id: number): Anime => ({ id, displayTitle: `番剧 ${id}`, originalTitle: '', season: '第 1 季', coverUrl: '', locked: false })

it('toggles grid/list, uses a local cover fallback and follows server nextOffset', async () => {
  const get = vi.spyOn(client, 'getAnime').mockResolvedValueOnce({ items: [anime(1)], nextOffset: 200 }).mockResolvedValueOnce({ items: [anime(201)], nextOffset: null })
  render(<LibraryPage onOpenAnime={vi.fn()} />)
  expect(await screen.findByRole('button', { name: '打开 番剧 1' })).toBeInTheDocument()
  expect(screen.getAllByText('暂无封面').length).toBeGreaterThan(0)
  fireEvent.click(screen.getByRole('button', { name: '列表视图' }))
  expect(screen.getByRole('table')).toBeInTheDocument()
  fireEvent.click(screen.getByRole('button', { name: '下一页' }))
  await waitFor(() => expect(get).toHaveBeenLastCalledWith(200, 200, expect.anything()))
  expect(await screen.findByRole('button', { name: '打开 番剧 201' })).toBeInTheDocument()
  expect(screen.getByRole('table')).toBeInTheDocument()
}, 20000)

it('shows a local placeholder if a nonempty cover URL fails to load', async () => {
  vi.spyOn(client, 'getAnime').mockResolvedValue({ items: [{ ...anime(1), coverUrl: 'https://example.invalid/cover.png' }], nextOffset: null })
  render(<LibraryPage onOpenAnime={vi.fn()} />)
  const image = await screen.findByRole('img', { name: '番剧 1 封面' })
  fireEvent.error(image)
  expect(screen.getByText('暂无封面')).toBeVisible()
  expect(screen.queryByRole('img', { name: '番剧 1 封面' })).not.toBeInTheDocument()
}, 20000)

it('filters the current library page and keeps binding status visible on poster cards', async () => {
  vi.spyOn(client, 'getAnime').mockResolvedValue({
    items: [{ ...anime(1), displayTitle: '透明之夜', bangumiSubjectId: 123 }, anime(2)], nextOffset: null,
  })
  render(<LibraryPage onOpenAnime={vi.fn()} />)
  expect(await screen.findByRole('button', { name: '打开 透明之夜' })).toBeInTheDocument()
  fireEvent.change(screen.getByRole('searchbox', { name: '筛选本页番剧' }), { target: { value: '透明' } })
  expect(screen.queryByRole('button', { name: '打开 番剧 2' })).not.toBeInTheDocument()
  expect(screen.getByText('已绑定 Bangumi')).toBeVisible()
}, 20000)
