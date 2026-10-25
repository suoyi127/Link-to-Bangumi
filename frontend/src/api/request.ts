import type { ApiResponse } from './types'

const bangumiErrorText: Record<string, string> = {
  bangumi_remote_rate_limited: 'Bangumi 服务端限流，已暂停发送请求，请稍后重试',
  bangumi_local_rate_limited: '本地请求额度暂时用尽，请稍后重试',
  bangumi_queue_full: '本地请求队列已满，请等待当前任务完成后重试',
}

export class ApiError extends Error {
  constructor(public code: string, public status: number, public requestId?: string, public targetAnimeId?: number, public targetTitle?: string) {
    super(`${bangumiErrorText[code] || '请求失败'}（${code}${requestId ? `，请求 ID：${requestId}` : ''}）`)
    this.name = 'ApiError'
  }
}

// 统一处理同源请求、API 错误对象和取消信号，页面组件只消费业务数据。
export async function request<T>(path: string, init?: RequestInit): Promise<ApiResponse<T>> {
  let response: Response
  try { response = await fetch(path, { ...init, headers: { Accept: 'application/json', ...init?.headers } }) }
  catch (error) {
    if (error instanceof DOMException && error.name === 'AbortError') throw new ApiError('request_aborted', 0)
    throw new ApiError('network_error', 0)
  }
  let body: unknown
  try { body = await response.json() }
  catch (error) {
    if (error instanceof DOMException && error.name === 'AbortError') throw new ApiError('request_aborted', 0)
    throw new ApiError('invalid_response', response.status, response.headers.get('X-Request-Id') ?? undefined)
  }
  if (typeof body !== 'object' || body === null || Array.isArray(body)) throw new ApiError('invalid_response', response.status, response.headers.get('X-Request-Id') ?? undefined)
  const payload = body as Record<string, unknown>
  const requestId = typeof payload.requestId === 'string' ? payload.requestId : response.headers.get('X-Request-Id') ?? undefined
  if (!response.ok) {
    const error = payload.error
    const code = typeof error === 'object' && error !== null && 'code' in error && typeof error.code === 'string'
      ? error.code
      : typeof payload.errorCode === 'string' && payload.errorCode ? payload.errorCode : 'http_error'
    throw new ApiError(code, response.status, requestId, typeof payload.targetAnimeId === 'number' ? payload.targetAnimeId : undefined, typeof payload.targetTitle === 'string' ? payload.targetTitle : undefined)
  }
  return payload as ApiResponse<T>
}

export const pageQuery = (limit: number, offset: number) => `?limit=${limit}&offset=${offset}`
export const json = (body: object): RequestInit => ({ method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body) })
