import { Alert, Button, List, Space, Spin } from 'antd'
import { auditPageSize } from './useAuditHistory'
import type { useAuditHistory } from './useAuditHistory'

export function AuditHistorySection({ controller }: { controller: ReturnType<typeof useAuditHistory> }) {
  const { audit, auditOffset, setAuditOffset, nextAuditOffset, auditLoading, auditError } = controller
  return <details className="settings-section" aria-label="审计记录">
    <summary style={{ cursor: 'pointer', fontSize: 16, fontWeight: 600 }}>审计记录</summary>
    <Space direction="vertical" size="middle" style={{ width: '100%', marginTop: 16 }}>
    {auditError && <Alert type="error" message={auditError} />}
    {auditLoading ? <Spin aria-label="正在加载审计记录" /> : <List dataSource={audit} locale={{ emptyText: '暂无审计记录' }} renderItem={(item) => <List.Item>{item.createdAt} · {item.action} · {item.entityType} #{item.entityId}</List.Item>} />}
    <Space><Button aria-label="上一页审计" disabled={auditLoading || auditOffset === 0} onClick={() => setAuditOffset(Math.max(0, auditOffset - auditPageSize))}>上一页</Button><Button aria-label="下一页审计" disabled={auditLoading || nextAuditOffset === null} onClick={() => { if (nextAuditOffset !== null) setAuditOffset(nextAuditOffset) }}>下一页</Button></Space>
    </Space>
    </details>
}
