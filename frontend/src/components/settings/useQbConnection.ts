import { useEffect, useState } from 'react'
import { deleteQbConfig, getQbConfig, putQbConfig, testQbConfig } from '../../api/qb'
import type { QbConfig, QbConfigDraft } from '../../api/types'
import { errorText, useRequestScope } from './useRequestScope'

export function useQbConnection(onChanged: (configured: boolean, cleared: boolean) => void, onOperationStart: () => void) {
  const [qbConfig, setQbConfig] = useState<QbConfig | null>(null)
  const [qbDraft, setQbDraft] = useState<QbConfigDraft>({ url: 'http://[::1]:8080', username: '', password: '' })
  const [qbConfigBusy, setQbConfigBusy] = useState(false)
  const [qbTestBusy, setQbTestBusy] = useState(false)
  const [qbConfigError, setQbConfigError] = useState('')
  const [qbTestMessage, setQbTestMessage] = useState('')
  const configuration = useRequestScope()
  const testing = useRequestScope()

  useEffect(() => {
    const request = configuration.start()
    void getQbConfig(request.signal).then((config) => {
      if (!request.isCurrent()) return
      setQbConfig(config)
      setQbDraft({ url: config.url, username: config.username, password: '' })
    }).catch((cause) => { if (request.isCurrent()) setQbConfigError(errorText(cause)) })
    return configuration.invalidate
  }, [configuration])

  async function update(cleared: boolean) {
    if (qbConfigBusy) return
    // 保存和清除会作废初始读取，连接测试使用独立代次。
    const request = configuration.start()
    setQbConfigBusy(true)
    setQbConfigError('')
    onOperationStart()
    setQbTestMessage('')
    try {
      const updated = cleared ? await deleteQbConfig() : await putQbConfig(qbDraft)
      if (!request.isCurrent()) return
      setQbConfig(updated)
      setQbDraft({ url: updated.url, username: updated.username, password: '' })
      onChanged(updated.configured, cleared)
    } catch (cause) {
      if (request.isCurrent()) setQbConfigError(errorText(cause))
    } finally {
      if (request.isCurrent()) setQbConfigBusy(false)
    }
  }

  async function testQbConnection() {
    if (qbTestBusy) return
    const request = testing.start()
    setQbTestBusy(true)
    setQbConfigError('')
    onOperationStart()
    setQbTestMessage('')
    try {
      const result = await testQbConfig(qbDraft)
      if (request.isCurrent()) setQbTestMessage(result.connected ? `测试成功 · qBittorrent ${result.version}` : `测试失败（${result.errorCode}）`)
    } catch (cause) {
      if (request.isCurrent()) setQbConfigError(errorText(cause))
    } finally {
      if (request.isCurrent()) setQbTestBusy(false)
    }
  }

  return {
    qbConfig, qbDraft, setQbDraft, qbConfigBusy, qbTestBusy, qbConfigError, qbTestMessage,
    saveQbConnection: () => update(false), clearQbConnection: () => update(true), testQbConnection,
  }
}
