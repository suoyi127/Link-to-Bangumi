import { useCallback, useEffect, useRef, useState } from 'react'
import { Alert, Button, Card, Col, Row, Space, Spin, Statistic, Table, Typography } from 'antd'
import { getAnime, getInbox, getScans, startImportScan, startSourceScan, syncMetadata } from '../api/client'
import type { MetadataSync, Scan } from '../api/types'
import { HealthStatus } from './HealthStatus'

type DashboardData = { inboxCount: number; animeCount: number; scans: Scan[] }

async function countAnime(signal?: AbortSignal) {
  let offset = 0
  let count = 0
  const seen = new Set<number>()
  while (true) {
    const page = await getAnime(200, offset, signal)
    count += page.items.length
    if (page.nextOffset === null) return count
    if (seen.has(page.nextOffset)) throw new Error('分页响应无效')
    seen.add(page.nextOffset)
    offset = page.nextOffset
  }
}

export function DashboardPage() {
  const [data, setData] = useState<DashboardData | null>(null)
  const [loading, setLoading] = useState(true)
  const [error, setError] = useState('')
  const [action, setAction] = useState<'source' | 'import' | 'metadata' | null>(null)
  const [syncResult, setSyncResult] = useState<MetadataSync | null>(null)
  const refreshSequence = useRef(0)
  const activeRefresh = useRef<AbortController | null>(null)

  const refresh = useCallback(async () => {
    activeRefresh.current?.abort()
    const controller = new AbortController()
    activeRefresh.current = controller
    const sequence = ++refreshSequence.current
    const current = () => !controller.signal.aborted && sequence === refreshSequence.current
    setLoading(true)
    setError('')
    try {
      const [inbox, animeCount, scans] = await Promise.all([getInbox(1, 0, controller.signal), countAnime(controller.signal), getScans(10, 0, controller.signal)])
      if (current()) setData({ inboxCount: inbox.total, animeCount, scans: scans.items })
    } catch (cause) {
      if (current()) setError(cause instanceof Error ? cause.message : '加载失败')
    } finally { if (current()) setLoading(false) }
  }, [])

  useEffect(() => {
    const sequence = refreshSequence
    const active = activeRefresh
    void refresh()
    return () => { sequence.current++; active.current?.abort() }
  }, [refresh])

  async function scan(kind: 'source' | 'import') {
    setAction(kind)
    setError('')
    try {
      if (kind === 'source') await startSourceScan()
      else await startImportScan()
      await refresh()
    } catch (cause) { setError(cause instanceof Error ? cause.message : '扫描失败') }
    finally { setAction(null) }
  }

  async function synchronizeMetadata() {
    setAction('metadata')
    setError(''); setSyncResult(null)
    try {
      const result = await syncMetadata()
      setSyncResult(result)
      await refresh()
    } catch (cause) { setError(cause instanceof Error ? cause.message : '元数据同步失败') }
    finally { setAction(null) }
  }

  const failed = data?.scans.filter((scan) => scan.errorCount > 0 || scan.status === 'failed') ?? []
  return <Space direction="vertical" size="large" style={{ width: '100%' }}>
    <HealthStatus />
    <Row gutter={16}>
      <Col xs={24} sm={12}><Card><Statistic title="待整理文件" value={data?.inboxCount ?? '—'} /></Card></Col>
      <Col xs={24} sm={12}><Card><Statistic title="番剧标题" value={data?.animeCount ?? '—'} /></Card></Col>
    </Row>
    <Space wrap>
      <Button type="primary" loading={action === 'source'} disabled={action !== null} onClick={() => void scan('source')}>扫描 qB 来源</Button>
      <Button loading={action === 'import'} disabled={action !== null} onClick={() => void scan('import')}>扫描外部导入</Button>
      <Button loading={action === 'metadata'} disabled={action !== null} onClick={() => void synchronizeMetadata()}>同步刮削元数据</Button>
      <Button onClick={() => void refresh()} disabled={loading || action !== null}>刷新</Button>
    </Space>
    {loading && <Spin aria-label="正在加载仪表盘" />}
    {error && <Alert type="error" showIcon message={error} />}
    {syncResult && <Alert type={syncResult.mikanErrorCode ? 'warning' : 'success'} showIcon message={`Mikan 更新 ${syncResult.mikanApplied} 部 · Bangumi 绑定 ${syncResult.bangumiBound} 部${syncResult.mikanErrorCode ? ` · ${syncResult.mikanErrorCode}` : ''}`} />}
    {!loading && data && <Card title="最近扫描">
      <Table rowKey="id" pagination={false} dataSource={data?.scans ?? []} locale={{ emptyText: '暂无扫描记录' }} columns={[
        { title: '编号', dataIndex: 'id' }, { title: '来源', dataIndex: 'source' }, { title: '状态', dataIndex: 'status' },
        { title: '发现', dataIndex: 'discoveredCount' }, { title: '处理', dataIndex: 'processedCount' }, { title: '错误', dataIndex: 'errorCount' },
      ]} />
    </Card>}
    {!loading && data && <Card title="最近失败">{failed.length ? failed.map((scan) => <Typography.Paragraph key={scan.id}>扫描 #{scan.id} · {scan.source} · {scan.errorCount} 个错误</Typography.Paragraph>) : <Typography.Text type="secondary">暂无失败记录</Typography.Text>}</Card>}
  </Space>
}
