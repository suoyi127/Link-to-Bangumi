import { cleanup, render, screen } from '@testing-library/react'
import { afterEach, beforeEach, expect, it, vi } from 'vitest'
import App from './App'

vi.mock('./components/DashboardPage', () => ({ DashboardPage: () => <div>Dashboard</div> }))

afterEach(() => { cleanup(); vi.restoreAllMocks() })
beforeEach(() => {
  vi.stubGlobal('matchMedia', vi.fn().mockImplementation(() => ({ matches: false, addListener: vi.fn(), removeListener: vi.fn(), addEventListener: vi.fn(), removeEventListener: vi.fn() })))
})

it('keeps the sidebar in view while the main page scrolls', () => {
  render(<App />)
  const sidebar = screen.getByText('Anime Vault').closest('aside')
  expect(sidebar).toHaveStyle({ position: 'sticky', top: '0px', height: '100vh', overflowY: 'auto' })
})

it('provides a named content landmark and mobile navigation', () => {
  render(<App />)
  expect(screen.getByRole('main', { name: '仪表盘内容' })).toBeInTheDocument()
  expect(screen.getByRole('navigation', { name: '快捷导航' })).toBeInTheDocument()
})
