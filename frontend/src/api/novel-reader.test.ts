import { afterEach, expect, it, vi } from 'vitest'
import { saveNovelReader } from './client'

afterEach(() => vi.restoreAllMocks())
it('does not send response metadata back when saving the reader', async () => {
  const fetch = vi.spyOn(globalThis, 'fetch').mockResolvedValue(new Response(JSON.stringify({ type: 'custom', executable: 'D:/Reader/Reader.exe' })))
  const config = { type: 'custom' as const, executable: 'D:/Reader/Reader.exe', requestId: 'novel-120' }
  await saveNovelReader(config)
  expect(fetch).toHaveBeenCalledWith('/api/novel-reader', expect.objectContaining({ body: JSON.stringify({ type: 'custom', executable: config.executable }) }))
})
