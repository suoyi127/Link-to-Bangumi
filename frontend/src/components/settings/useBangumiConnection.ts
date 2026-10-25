import { useEffect, useState } from 'react'
import { deleteBangumiConfig, getBangumiConfig, putBangumiConfig, testBangumiConfig } from '../../api/settings'
import type { BangumiConfig } from '../../api/types'
import { errorText, useRequestScope } from './useRequestScope'

const suggestedBangumiAgent = 'suoyi127/Link-to-Bangumi/0.1 (Windows) (https://github.com/suoyi127/Link-to-Bangumi)'

export function useBangumiConnection(onChanged: (configured: boolean) => void) {
  const [bangumiConfig, setBangumiConfig] = useState<BangumiConfig | null>(null)
  const [bangumiAgent, setBangumiAgent] = useState(suggestedBangumiAgent)
  const [bangumiBusy, setBangumiBusy] = useState(false)
  const [bangumiTesting, setBangumiTesting] = useState(false)
  const [bangumiError, setBangumiError] = useState('')
  const [bangumiTestMessage, setBangumiTestMessage] = useState('')
  const configuration = useRequestScope()
  const testing = useRequestScope()

  useEffect(() => {
    const request = configuration.start()
    void getBangumiConfig(request.signal).then((config) => {
      if (!request.isCurrent()) return
      setBangumiConfig(config)
      setBangumiAgent(config.userAgent || suggestedBangumiAgent)
    }).catch((cause) => { if (request.isCurrent()) setBangumiError(errorText(cause)) })
    return configuration.invalidate
  }, [configuration])

  async function update(cleared: boolean) {
    if (bangumiBusy) return
    const request = configuration.start()
    setBangumiBusy(true)
    setBangumiError('')
    setBangumiTestMessage('')
    try {
      const updated = cleared ? await deleteBangumiConfig() : await putBangumiConfig(bangumiAgent)
      if (!request.isCurrent()) return
      setBangumiConfig(updated)
      if (cleared) setBangumiAgent(updated.userAgent || suggestedBangumiAgent)
      onChanged(updated.configured)
    } catch (cause) {
      if (request.isCurrent()) setBangumiError(errorText(cause))
    } finally {
      if (request.isCurrent()) setBangumiBusy(false)
    }
  }

  async function testBangumiConnection() {
    if (bangumiTesting) return
    const request = testing.start()
    setBangumiTesting(true)
    setBangumiError('')
    setBangumiTestMessage('')
    try {
      const result = await testBangumiConfig(bangumiAgent)
      if (request.isCurrent()) setBangumiTestMessage(result.connected ? '测试成功 · Bangumi 公共接口可访问' : `测试失败（${result.errorCode}）`)
    } catch (cause) {
      if (request.isCurrent()) setBangumiError(errorText(cause))
    } finally {
      if (request.isCurrent()) setBangumiTesting(false)
    }
  }

  return {
    bangumiConfig, bangumiAgent, setBangumiAgent, bangumiBusy, bangumiTesting, bangumiError, bangumiTestMessage,
    saveBangumiConnection: () => update(false), clearBangumiConnection: () => update(true), testBangumiConnection,
  }
}
