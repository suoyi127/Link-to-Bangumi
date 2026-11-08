import { useState } from 'react'
import { Alert, Button, Checkbox, Input, List, Popconfirm, Space, Spin, Typography } from 'antd'
import { collectWatchSubject, getWatchSync, saveWatchSync, type WatchSync } from '../../api/watch'
import { useWatchSnapshot } from '../useWatchSnapshot'
import { ApiError } from '../../api/request'
import { SettingsSection } from './SettingsSection'

const issueText: Record<string, string> = {
  subject_not_collected: '作品尚未加入 Bangumi 收藏',
  subject_unlinked: '请先在番剧详情页绑定 Bangumi 条目',
  episode_unmapped: '没有匹配的 Bangumi 集数，请检查本地集数和类型',
  episode_ambiguous: '匹配到多个 Bangumi 集数，请检查作品绑定和本地集数',
  bangumi_unauthorized: 'Bangumi 拒绝了令牌，请重新填写有效的个人访问令牌',
  bangumi_forbidden: '当前账号没有写入该收藏的权限',
  bangumi_rate_limited: 'Bangumi 请求过多，稍后会重试',
  bangumi_network_error: '暂时无法连接 Bangumi',
  bangumi_http_error: 'Bangumi 请求失败，稍后会重试',
  bangumi_invalid_response: 'Bangumi 返回了无法识别的数据',
  credential_missing: '找不到本机保存的令牌，请重新填写',
  credential_storage_failed: '无法读写本机保存的令牌',
  invalid_bangumi_token: '请填写有效的 Bangumi 个人访问令牌',
  watch_sync_unavailable: 'Bangumi 同步服务暂不可用',
  sync_busy: '同步服务正忙，请稍后再试',
  collection_not_pending: '该作品的待收藏状态已变化，请查看最新同步状态',
  mikan_subject_unmapped: 'Mikan 番剧页缺少对应的 Bangumi 条目，请打开番剧页核对。',
  mikan_unavailable: '暂时无法读取 Mikan 番剧页，请稍后查看同步状态。',
}

export function WatchSyncSection() {
  const { data, error, update } = useWatchSnapshot(getWatchSync)
  const [enabled, setEnabled] = useState<boolean | null>(null)
  const [token, setToken] = useState('')
  const [busy, setBusy] = useState(false)
  const [feedback, setFeedback] = useState('')
  const [notice, setNotice] = useState('')
  async function submit(operation: () => Promise<WatchSync>, message: string, saved = false) {
    if (busy) return
    setBusy(true); setFeedback(''); setNotice('')
    try {
      update(await operation())
      if (saved) { setToken(''); setEnabled(null) }
      setNotice(message)
    } catch (cause) {
      const message = cause instanceof ApiError && issueText[cause.code] ? issueText[cause.code] : cause instanceof Error ? cause.message : '请求失败'
      setFeedback(message)
    }
    finally { setBusy(false) }
  }
  const checked = enabled ?? data?.enabled ?? false
  return <SettingsSection title="Bangumi 观看同步">
    <Typography.Text type="secondary">启用后，新标记为已看的分集会同步到 Bangumi。标为未看只重置本地记录。播放位置保存在本机。关闭同步并保存会清除本机保存的令牌。</Typography.Text>
    <Typography.Text type="secondary">同步开启期间，新添加的 Mikan 单番订阅会将尚未收藏的作品加入 Bangumi「在看」。已收藏作品保留原状态。聚合和搜索订阅不触发收藏。</Typography.Text>
    {error && <Alert type="warning" message={`同步状态读取失败：${error}`} />}
    {feedback && <Alert type="error" message={feedback} />}
    {notice && <Alert type="success" message={notice} />}
    {!data && !error && <Spin aria-label="正在读取同步状态" />}
    {data && <>
      <Typography.Text>{data.enabled ? data.connected ? `已连接 Bangumi · ${data.username}` : '同步已开启 · 未连接' : '同步已关闭'} · 分集待同步 {data.pending} 项 · 订阅待收藏 {data.subscriptionPending ?? 0} 项</Typography.Text>
      {data.errorCode && <Alert type="warning" message={issueText[data.errorCode] || data.errorCode} />}
      <Checkbox checked={checked} disabled={busy} onChange={event => setEnabled(event.target.checked)}>同步本地已看状态到 Bangumi</Checkbox>
      <label>Bangumi 个人访问令牌<Input.Password aria-label="Bangumi 个人访问令牌" autoComplete="new-password" maxLength={2048} value={token} disabled={busy} placeholder={data.connected ? '留空以沿用已保存的令牌' : '输入个人访问令牌'} onChange={event => setToken(event.target.value)} /></label>
      <a href="https://next.bgm.tv/demo/access-token" target="_blank" rel="noopener noreferrer">在 Bangumi 创建个人访问令牌</a>
      <Button loading={busy} onClick={() => void submit(() => saveWatchSync(checked, checked && token.trim() || undefined), '观看同步设置已保存', true)}>保存观看同步设置</Button>
      {!!data.issues?.length && <List size="small" dataSource={data.issues} renderItem={issue => {
        const title = issue.title || `媒体 #${issue.mediaId}`
        return <List.Item><Space wrap>
          <Typography.Text>{title} {issue.episodeNumber} · {issueText[issue.errorCode] || issue.errorCode}</Typography.Text>
          {issue.errorCode === 'subject_not_collected' && data.connected && data.enabled && <Popconfirm title={`将「${title}」加入 Bangumi 在看收藏？`} description="收藏后将继续同步该作品的待处理集数。" okText="确认收藏" cancelText="取消" onConfirm={() => submit(() => collectWatchSubject(issue.mediaId), '已提交收藏请求')}>
            <Button disabled={busy} aria-label={`收藏并同步 ${title}`}>收藏并同步</Button>
          </Popconfirm>}
        </Space></List.Item>
      }} />}
      {!!data.subscriptionIssues?.length && <List size="small" rowKey="mikanId" dataSource={data.subscriptionIssues} renderItem={issue => <List.Item><Space wrap>
        <a href={`https://mikanani.me/Home/Bangumi/${issue.mikanId}`} target="_blank" rel="noopener noreferrer">Mikan 番剧 #{issue.mikanId}</a>
        <Typography.Text>{issueText[issue.errorCode] || `收藏失败（${issue.errorCode}）`}</Typography.Text>
      </Space></List.Item>} />}
    </>}
  </SettingsSection>
}
