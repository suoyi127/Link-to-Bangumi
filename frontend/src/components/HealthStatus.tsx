import { useEffect, useState } from 'react'
import { Alert, Card, Spin } from 'antd'
import { getHealth } from '../api/health'

type Status = 'loading' | 'healthy' | 'unavailable'

export function HealthStatus() {
  const [status, setStatus] = useState<Status>('loading')

  useEffect(() => {
    let active = true
    getHealth()
      .then(() => { if (active) setStatus('healthy') })
      .catch(() => { if (active) setStatus('unavailable') })
    return () => { active = false }
  }, [])

  return (
    <Card title="后端服务">
      {status === 'loading' && <Spin aria-label="正在检查服务" />}
      {status === 'healthy' && <Alert type="success" showIcon message="服务正常" />}
      {status === 'unavailable' && <Alert type="error" showIcon message="服务不可用" />}
    </Card>
  )
}
