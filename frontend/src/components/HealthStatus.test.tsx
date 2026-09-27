import { render, screen } from '@testing-library/react'
import { afterEach, expect, it, vi } from 'vitest'
import * as healthApi from '../api/health'
import { HealthStatus } from './HealthStatus'

afterEach(() => vi.restoreAllMocks())

it('shows a healthy backend', async () => {
  vi.spyOn(healthApi, 'getHealth').mockResolvedValue({ status: 'ok', service: 'anime-vault' })
  render(<HealthStatus />)
  expect(await screen.findByText('服务正常')).toBeInTheDocument()
})
