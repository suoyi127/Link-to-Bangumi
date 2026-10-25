import { Space, Typography } from 'antd'
import type { ReactNode } from 'react'

export function SettingsSection({ title, wide = false, children }: { title: string; wide?: boolean; children: ReactNode }) {
  return <section className={`settings-section${wide ? ' wide' : ''}`} aria-label={title}>
    <Space direction="vertical" size="middle" style={{ width: '100%' }}>
      <Typography.Title level={4}>{title}</Typography.Title>
      {children}
    </Space>
  </section>
}
