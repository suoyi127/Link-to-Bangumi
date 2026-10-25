import { useCallback, useState } from 'react'
import { addMikanFeed, createMikanRule, getMikanFeeds, getQbStatus } from '../../api/qb'
import type { MikanFeeds, QbStatus } from '../../api/types'
import { errorText, useRequestScope } from './useRequestScope'

export function useMikanSubscriptions() {
  const [qbStatus, setQbStatus] = useState<QbStatus | null>(null)
  const [statusError, setStatusError] = useState('')
  const [mikanFeeds, setMikanFeeds] = useState<MikanFeeds | null>(null)
  const [mikanError, setMikanError] = useState('')
  const [feedUrl, setFeedUrl] = useState('')
  const [addingFeed, setAddingFeed] = useState(false)
  const [ruleName, setRuleName] = useState('')
  const [ruleFeedUrl, setRuleFeedUrl] = useState('')
  const [ruleKeyword, setRuleKeyword] = useState('')
  const [creatingRule, setCreatingRule] = useState(false)
  const [ruleMessage, setRuleMessage] = useState('')
  const statusRequests = useRequestScope()
  const feedRequests = useRequestScope()
  const feedActions = useRequestScope()
  const ruleActions = useRequestScope()

  const clearStatusError = useCallback(() => setStatusError(''), [])

  const refreshStatus = useCallback(async (initial = false) => {
    const request = statusRequests.start()
    setStatusError('')
    try {
      const status = await getQbStatus(request.signal)
      if (request.isCurrent()) setQbStatus(status)
    } catch (cause) {
      if (!request.isCurrent()) return
      if (initial) setQbStatus({ configured: true, connected: false, errorCode: 'network_error', version: '', torrentCount: 0, completedCount: 0 })
      else setStatusError(errorText(cause))
    }
  }, [statusRequests])

  const refreshFeeds = useCallback(async (errorMode: 'show' | 'throw' | 'preserve' = 'show') => {
    const request = feedRequests.start()
    try {
      const feeds = await getMikanFeeds(request.signal)
      if (request.isCurrent()) setMikanFeeds(feeds)
    } catch (cause) {
      if (!request.isCurrent()) return
      if (errorMode === 'throw') throw cause
      if (errorMode === 'show') setMikanError(errorText(cause))
    }
  }, [feedRequests])

  const connectionChanged = useCallback((configured: boolean, cleared: boolean) => {
    statusRequests.invalidate()
    feedRequests.invalidate()
    feedActions.invalidate()
    ruleActions.invalidate()
    setAddingFeed(false)
    setCreatingRule(false)
    setStatusError('')
    if (cleared) {
      setQbStatus(null)
      setMikanFeeds(null)
      // 环境回退只重新检查连接；清除后不主动重载订阅。
      if (configured) void refreshStatus()
    } else {
      void refreshStatus()
      void refreshFeeds()
    }
  }, [statusRequests, feedRequests, feedActions, ruleActions, refreshStatus, refreshFeeds])

  async function addFeed() {
    if (addingFeed) return
    const request = feedActions.start()
    setAddingFeed(true)
    setMikanError('')
    try {
      const result = await addMikanFeed(feedUrl.trim())
      if (!request.isCurrent()) return
      if (!result.success) throw new Error(result.errorCode)
      setFeedUrl('')
      await refreshFeeds('throw')
    } catch (cause) {
      if (!request.isCurrent()) return
      setMikanError(`${errorText(cause)}；若订阅已出现但自动规则未建立，请检查 qB RSS 状态。`)
      // 添加或首次读取失败时重试订阅读取，同时保留操作警告。
      await refreshFeeds('preserve')
    } finally {
      if (request.isCurrent()) setAddingFeed(false)
    }
  }

  async function addRule() {
    if (creatingRule) return
    const request = ruleActions.start()
    setCreatingRule(true)
    setRuleMessage('')
    try {
      const result = await createMikanRule({ ruleName: ruleName.trim(), feedUrl: ruleFeedUrl.trim(), keyword: ruleKeyword.trim() })
      if (!request.isCurrent()) return
      if (!result.success) throw new Error(result.errorCode)
      setRuleMessage('自动下载规则已创建；请在 qB 中检查匹配结果。')
      setRuleName('')
      setRuleFeedUrl('')
      setRuleKeyword('')
    } catch (cause) {
      if (request.isCurrent()) setRuleMessage(errorText(cause))
    } finally {
      if (request.isCurrent()) setCreatingRule(false)
    }
  }

  return {
    qbStatus, statusError, clearStatusError, mikanFeeds, mikanError,
    feedUrl, setFeedUrl, addingFeed, ruleName, setRuleName, ruleFeedUrl, setRuleFeedUrl,
    ruleKeyword, setRuleKeyword, creatingRule, ruleMessage, refreshStatus, refreshFeeds, connectionChanged, addFeed, addRule,
  }
}
