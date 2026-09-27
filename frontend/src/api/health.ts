export type Health = { status: 'ok'; service: 'anime-vault' }

export async function getHealth(): Promise<Health> {
  const response = await fetch('/health')
  if (!response.ok) throw new Error('健康检查请求失败')

  const payload: unknown = await response.json()
  if (
    typeof payload !== 'object' || payload === null ||
    !('status' in payload) || payload.status !== 'ok' ||
    !('service' in payload) || payload.service !== 'anime-vault'
  ) {
    throw new Error('健康检查响应无效')
  }
  return { status: 'ok', service: 'anime-vault' }
}
