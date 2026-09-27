import { useEffect, useRef, useState } from 'react'
import { Alert, Button, Descriptions, Input, InputNumber, List, Popconfirm, Space, Spin, Typography } from 'antd'
import { addMikanFeed, createMikanRule, getAuditLogs, getMikanFeeds, getQbStatus, getSettings, putSettings } from '../api/client'
import type { AuditLog, MikanFeeds, Preferences, QbStatus, Settings } from '../api/types'

const auditPageSize = 50
const errorText = (error: unknown) => error instanceof Error ? error.message : '请求失败'

export function SettingsPage() {
  const [settings, setSettings] = useState<Settings | null>(null)
  const [draft, setDraft] = useState<Preferences | null>(null)
  const [settingsError, setSettingsError] = useState('')
  const [qbStatus, setQbStatus] = useState<QbStatus | null>(null)
  const [mikanFeeds, setMikanFeeds] = useState<MikanFeeds | null>(null)
  const [mikanError, setMikanError] = useState('')
  const [feedUrl, setFeedUrl] = useState('')
  const [addingFeed, setAddingFeed] = useState(false)
  const [ruleName, setRuleName] = useState('')
  const [ruleFeedUrl, setRuleFeedUrl] = useState('')
  const [ruleKeyword, setRuleKeyword] = useState('')
  const [creatingRule, setCreatingRule] = useState(false)
  const [ruleMessage, setRuleMessage] = useState('')
  const [saving, setSaving] = useState(false)
  const [audit, setAudit] = useState<AuditLog[]>([])
  const [auditOffset, setAuditOffset] = useState(0)
  const [nextAuditOffset, setNextAuditOffset] = useState<number | null>(null)
  const [auditLoading, setAuditLoading] = useState(true)
  const [auditError, setAuditError] = useState('')
  const auditSequence = useRef(0)

  useEffect(() => {
    let active = true
    void getSettings().then((value) => {
      if (!active) return
      setSettings(value)
      setDraft({ preferredOperation: value.preferredOperation, scanIntervalSeconds: value.scanIntervalSeconds, mpvExecutable: value.mpvExecutable, qbWebUiUrl: value.qbWebUiUrl })
      if (value.qbWebUiConfigured) void getQbStatus().then((status) => { if (active) setQbStatus(status) })
        .catch(() => { if (active) setQbStatus({ configured: true, connected: false, errorCode: 'network_error', version: '', torrentCount: 0, completedCount: 0 }) })
      if (value.qbWebUiConfigured) void getMikanFeeds().then((feeds) => { if (active) setMikanFeeds(feeds) })
        .catch((cause) => { if (active) setMikanError(errorText(cause)) })
    }).catch((cause) => { if (active) setSettingsError(errorText(cause)) })
    return () => { active = false }
  }, [])
  useEffect(() => {
    const sequenceRef = auditSequence
    const current = ++sequenceRef.current
    setAuditLoading(true); setAuditError('')
    void getAuditLogs(auditPageSize, auditOffset).then((page) => {
      if (current !== sequenceRef.current) return
      setAudit(page.items); setNextAuditOffset(page.nextOffset)
    }).catch((cause) => { if (current === sequenceRef.current) setAuditError(errorText(cause)) })
      .finally(() => { if (current === sequenceRef.current) setAuditLoading(false) })
    return () => { if (sequenceRef.current === current) sequenceRef.current++ }
  }, [auditOffset])

  async function save() {
    if (!draft || saving) return
    setSaving(true); setSettingsError('')
    try {
      const updated = await putSettings(draft)
      setSettings(updated)
      setDraft({ preferredOperation: updated.preferredOperation, scanIntervalSeconds: updated.scanIntervalSeconds, mpvExecutable: updated.mpvExecutable, qbWebUiUrl: updated.qbWebUiUrl })
    } catch (cause) { setSettingsError(errorText(cause)) }
    finally { setSaving(false) }
  }

  async function addFeed() {
    if (addingFeed) return
    setAddingFeed(true); setMikanError('')
    try {
      const result = await addMikanFeed(feedUrl.trim())
      if (!result.success) throw new Error(result.errorCode)
      setFeedUrl('')
      setMikanFeeds(await getMikanFeeds())
    } catch (cause) {
      setMikanError(`${errorText(cause)}；若订阅已出现但自动规则未建立，请检查 qB RSS 状态。`)
      try { setMikanFeeds(await getMikanFeeds()) } catch { /* Keep the action error visible. */ }
    }
    finally { setAddingFeed(false) }
  }

  async function addRule() {
    if (creatingRule) return
    setCreatingRule(true); setRuleMessage('')
    try {
      const result = await createMikanRule({ ruleName: ruleName.trim(), feedUrl: ruleFeedUrl.trim(), keyword: ruleKeyword.trim() })
      if (!result.success) throw new Error(result.errorCode)
      setRuleMessage('自动下载规则已创建；请在 qB 中检查匹配结果。')
      setRuleName(''); setRuleFeedUrl(''); setRuleKeyword('')
    } catch (cause) { setRuleMessage(errorText(cause)) }
    finally { setCreatingRule(false) }
  }

  return <Space direction="vertical" size="large" style={{ width: '100%' }}>
    <Typography.Title level={3}>设置</Typography.Title>
    {settingsError && <Alert type="error" message={settingsError} />}
    {!settings && !settingsError && <Spin aria-label="正在加载设置" />}
    {settings && draft && <>
      <Typography.Title level={4}>生效路径（只读，修改环境变量后重启）</Typography.Title>
      <Descriptions bordered column={1} items={[
        { key: 'source', label: 'qB 下载目录', children: settings.sourcePath },
        { key: 'import', label: '外来导入目录', children: settings.importPath },
        { key: 'library', label: '媒体库目录', children: settings.libraryPath },
        { key: 'data', label: '数据目录', children: settings.dataPath },
      ]} />
      <Alert type="info" message={settings.bangumiConfigured ? 'Bangumi 已配置（直接联网）' : 'Bangumi 未配置；本地浏览仍可用'} />
      <Alert type={qbStatus?.connected ? 'success' : 'info'} message={qbStatus?.connected ? `qBittorrent 已连接 · ${qbStatus.version} · ${qbStatus.torrentCount} 个任务（${qbStatus.completedCount} 个已下载）` : settings.qbWebUiConfigured ? `qBittorrent 已配置，${qbStatus ? `连接失败（${qbStatus.errorCode}）` : '正在检查连接'}` : 'qBittorrent Web UI 未启用；qB 下载完成需手动确认'} />
      <Typography.Title level={4}>Mikan 订阅（由 qB 获取 RSS）</Typography.Title>
      {mikanFeeds && <Typography.Text>{mikanFeeds.feedCount} 个订阅 · {mikanFeeds.articleCount} 篇文章 · {mikanFeeds.pairCount} 组标题映射</Typography.Text>}
      {mikanFeeds?.errorCode && <Alert type="warning" message={mikanFeeds.errorCode} />}
      {mikanError && <Alert type="warning" message={mikanError} />}
      <List size="small" dataSource={mikanFeeds?.feeds ?? []} locale={{ emptyText: '暂无可读取的 Mikan 订阅' }} renderItem={(feed) => <List.Item>{feed.title || 'Mikan 订阅'} · {feed.articleCount} 篇{feed.hasError ? ' · qB 获取失败' : ''}</List.Item>} />
      <Typography.Text type="secondary">新订阅首次获取完成后，已有条目将标记已读；随后自动建立每集下载首个可识别资源的 qB 规则，不限定画质或字幕。无法识别集数的条目会跳过。</Typography.Text>
      <Space.Compact><Input aria-label="Mikan RSS 地址" value={feedUrl} onChange={(event) => setFeedUrl(event.target.value)} maxLength={1024} placeholder="https://mikanani.me/RSS/Bangumi?..." /><Button loading={addingFeed} disabled={!settings.qbWebUiConfigured || !feedUrl.trim()} onClick={() => void addFeed()}>添加 Mikan 订阅</Button></Space.Compact>
      <Typography.Title level={5}>qB 自动下载规则</Typography.Title>
      <Typography.Text type="secondary">先将订阅加入 qB，再填写该订阅的完整地址。关键词按文章标题包含匹配；同名规则不会覆盖。下载目标固定为 {settings.sourcePath}。</Typography.Text>
      {ruleMessage && <Alert type={ruleMessage.startsWith('自动') ? 'success' : 'warning'} message={ruleMessage} />}
      <Input aria-label="规则名称" value={ruleName} onChange={(event) => setRuleName(event.target.value)} maxLength={80} placeholder="规则名称" />
      <Input aria-label="规则订阅地址" value={ruleFeedUrl} onChange={(event) => setRuleFeedUrl(event.target.value)} maxLength={1024} placeholder="https://mikanani.me/RSS/Bangumi?..." />
      <Input aria-label="标题包含关键词" value={ruleKeyword} onChange={(event) => setRuleKeyword(event.target.value)} maxLength={120} placeholder="例如字幕组名称或番剧名" />
      <Popconfirm title="确认创建自动下载规则？" description={`启用后 qB 可能立即下载已发布的匹配条目，保存到 ${settings.sourcePath}。`} okText="确认创建" cancelText="取消" onConfirm={() => void addRule()}>
        <Button loading={creatingRule} disabled={!settings.qbWebUiConfigured || !ruleName.trim() || !ruleFeedUrl.trim() || !ruleKeyword.trim()}>创建自动下载规则</Button>
      </Popconfirm>
      <Typography.Title level={4}>偏好</Typography.Title>
      <label>首选整理方式 <select aria-label="首选整理方式" value={draft.preferredOperation} onChange={(event) => setDraft({ ...draft, preferredOperation: event.target.value as Preferences['preferredOperation'] })}><option value="hardlink">硬链接</option><option value="copy">复制</option><option value="symlink">符号链接</option></select></label>
      <label>扫描间隔（预留，尚无自动扫描） <InputNumber aria-label="扫描间隔（秒）" min={60} max={86400} value={draft.scanIntervalSeconds} onChange={(value) => setDraft({ ...draft, scanIntervalSeconds: value ?? 3600 })} /></label>
        <label>mpv 可执行文件（本机播放） <Input aria-label="mpv 可执行文件" maxLength={1024} value={draft.mpvExecutable} onChange={(event) => setDraft({ ...draft, mpvExecutable: event.target.value })} /></label>
      <label>qB Web UI 地址（旧偏好字段；本机连接固定使用 IPv6 回环） <Input aria-label="qB Web UI 地址" maxLength={2048} value={draft.qbWebUiUrl} onChange={(event) => setDraft({ ...draft, qbWebUiUrl: event.target.value })} /></label>
      <Button type="primary" loading={saving} onClick={() => void save()}>保存偏好</Button>
    </>}
    <Typography.Title level={4}>审计记录</Typography.Title>
    {auditError && <Alert type="error" message={auditError} />}
    {auditLoading ? <Spin aria-label="正在加载审计记录" /> : <List dataSource={audit} locale={{ emptyText: '暂无审计记录' }} renderItem={(item) => <List.Item>{item.createdAt} · {item.action} · {item.entityType} #{item.entityId}</List.Item>} />}
    <Space><Button aria-label="上一页审计" disabled={auditLoading || auditOffset === 0} onClick={() => setAuditOffset(Math.max(0, auditOffset - auditPageSize))}>上一页</Button><Button aria-label="下一页审计" disabled={auditLoading || nextAuditOffset === null} onClick={() => { if (nextAuditOffset !== null) setAuditOffset(nextAuditOffset) }}>下一页</Button></Space>
  </Space>
}
