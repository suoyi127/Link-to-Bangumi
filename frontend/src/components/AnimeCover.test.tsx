import { cleanup, render, screen } from '@testing-library/react'
import { afterEach, expect, it } from 'vitest'
import { AnimeCover } from './AnimeCover'

afterEach(cleanup)

it('keeps intrinsic image dimensions out of the responsive poster layout', () => {
  render(<AnimeCover item={{ id: 1, displayTitle: '番剧', originalTitle: '', season: '', coverUrl: '/cover.jpg', locked: false }} />)
  const image = screen.getByRole('img', { name: '番剧 封面' })
  expect(image).toHaveStyle({ position: 'absolute', inset: '0' })
  expect(image.parentElement).toHaveStyle('position: relative; aspect-ratio: 2 / 3; min-width: 0')
})
