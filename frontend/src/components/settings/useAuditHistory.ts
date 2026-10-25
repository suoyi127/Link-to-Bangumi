import { useEffect, useState } from 'react'
import { getAuditLogs } from '../../api/settings'
import type { AuditLog } from '../../api/types'
import { errorText, useRequestScope } from './useRequestScope'

export const auditPageSize = 50

export function useAuditHistory() {
  const [audit, setAudit] = useState<AuditLog[]>([])
  const [auditOffset, setAuditOffset] = useState(0)
  const [nextAuditOffset, setNextAuditOffset] = useState<number | null>(null)
  const [auditLoading, setAuditLoading] = useState(true)
  const [auditError, setAuditError] = useState('')
  const requests = useRequestScope()

  useEffect(() => {
    const request = requests.start()
    setAuditLoading(true)
    setAuditError('')
    void getAuditLogs(auditPageSize, auditOffset, undefined, request.signal).then((page) => {
      if (!request.isCurrent()) return
      setAudit(page.items)
      setNextAuditOffset(page.nextOffset)
    }).catch((cause) => { if (request.isCurrent()) setAuditError(errorText(cause)) })
      .finally(() => { if (request.isCurrent()) setAuditLoading(false) })
    return requests.invalidate
  }, [auditOffset, requests])

  return { audit, auditOffset, setAuditOffset, nextAuditOffset, auditLoading, auditError }
}
