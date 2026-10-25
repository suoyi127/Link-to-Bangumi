import type { QbStatus, QbConfig, QbConfigDraft, MikanFeeds, QbAction, QbMikanRule } from './types'
import { request, json } from './request'

// 严格写入接口只接收配置字段，不能直接回传响应对象。
export const getQbStatus = (signal?: AbortSignal) => request<QbStatus>('/api/qb/status', signal ? { signal } : undefined)
export const getQbConfig = (signal?: AbortSignal) => request<QbConfig>('/api/qb/config', signal ? { signal } : undefined)
export const putQbConfig = (draft: QbConfigDraft) => request<QbConfig>('/api/qb/config', { ...json({ url: draft.url, username: draft.username, password: draft.password }), method: 'PUT' })
export const testQbConfig = (draft: QbConfigDraft) => request<QbStatus>('/api/qb/config/test', json({ url: draft.url, username: draft.username, password: draft.password }))
export const deleteQbConfig = () => request<QbConfig>('/api/qb/config', { method: 'DELETE' })
export const getMikanFeeds = (signal?: AbortSignal) => request<MikanFeeds>('/api/qb/rss', signal ? { signal } : undefined)
export const addMikanFeed = (url: string) => request<QbAction>('/api/qb/rss/feeds', json({ url }))
export const createMikanRule = (rule: QbMikanRule) => request<QbAction>('/api/qb/rss/rules', json({ ruleName: rule.ruleName, feedUrl: rule.feedUrl, keyword: rule.keyword }))
