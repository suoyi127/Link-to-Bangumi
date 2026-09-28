import { useEffect, useRef, useState } from 'react'
import { Alert, Button, Descriptions, Input, InputNumber, List, Popconfirm, Space, Spin, Typography } from 'antd'
import { addMikanFeed, createMikanRule, deleteQbConfig, getAuditLogs, getMikanFeeds, getQbConfig, getQbStatus, getSettings, putQbConfig, putQbDownloadDirectory, putSettings, testQbConfig } from '../api/client'
import type { AuditLog, MikanFeeds, Preferences, QbConfig, QbConfigDraft, QbStatus, Settings } from '../api/types'

const auditPageSize = 50
const errorText = (error: unknown) => error instanceof Error ? error.message : '请求失败'

export function SettingsPage() {
  const [settings, setSettings] = useState<Settings | null>(null)
  const [draft, setDraft] = useState<Preferences | null>(null)
  const [qbPath, setQbPath] = useState('')
  const [qbPathSaving, setQbPathSaving] = useState(false)
  const [restartRequired, setRestartRequired] = useState(false)
  const [settingsError, setSettingsError] = useState('')
  const [qbStatus, setQbStatus] = useState<QbStatus | null>(null)
  const [qbConfig, setQbConfig] = useState<QbConfig | null>(null)
  const [qbDraft, setQbDraft] = useState<QbConfigDraft>({ url: 'http://[::1]:8080', username: '', password: '' })
  const [qbConfigBusy, setQbConfigBusy] = useState(false)
  const [qbTestBusy, setQbTestBusy] = useState(false)
  const [qbConfigError, setQbConfigError] = useState('')
  const [qbTestMessage, setQbTestMessage] = useState('')
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
  const qbPathPending = restartRequired || Boolean(!settings?.qbDownloadEnvironmentOverride && settings?.qbDownloadDirectory &&
    settings.qbDownloadDirectory !== settings.sourcePath)

  useEffect(() => {
    let active = true
    void getQbConfig().then((config) => {
      if (!active) return
      setQbConfig(config)
      setQbDraft({ url: config.url, username: config.username, password: '' })
    }).catch((cause) => { if (active) setQbConfigError(errorText(cause)) })
    void getSettings().then((value) => {
      if (!active) return
      setSettings(value)
      setQbPath(value.qbDownloadDirectory)
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

  async function saveQbPath() {
    if (qbPathSaving) return
    setQbPathSaving(true); setSettingsError('')
    try {
      const updated = await putQbDownloadDirectory(qbPath.trim())
      setSettings(updated)
      setQbPath(updated.qbDownloadDirectory)
      setRestartRequired(updated.restartRequired)
    } catch (cause) { setSettingsError(errorText(cause)) }
    finally { setQbPathSaving(false) }
  }

  async function saveQbConnection() {
    if (qbConfigBusy) return
    setQbConfigBusy(true); setQbConfigError(''); setQbTestMessage('')
    try {
      const updated = await putQbConfig(qbDraft)
      setQbConfig(updated)
      setQbDraft({ url: updated.url, username: updated.username, password: '' })
      setSettings((previous) => previous && { ...previous, qbWebUiConfigured: updated.configured })
      void getQbStatus().then(setQbStatus).catch((cause) => setQbConfigError(errorText(cause)))
      void getMikanFeeds().then(setMikanFeeds).catch((cause) => setMikanError(errorText(cause)))
    } catch (cause) { setQbConfigError(errorText(cause)) }
    finally { setQbConfigBusy(false) }
  }

  async function testQbConnection() {
    if (qbTestBusy) return
    setQbTestBusy(true); setQbConfigError(''); setQbTestMessage('')
    try {
      const result = await testQbConfig(qbDraft)
      setQbTestMessage(result.connected ? `测试成功 · qBittorrent ${result.version}` : `测试失败（${result.errorCode}）`)
    } catch (cause) { setQbConfigError(errorText(cause)) }
    finally { setQbTestBusy(false) }
  }

  async function clearQbConnection() {
    if (qbConfigBusy) return
    setQbConfigBusy(true); setQbConfigError(''); setQbTestMessage('')
    try {
      const updated = await deleteQbConfig()
      setQbConfig(updated)
      setQbDraft({ url: updated.url, username: updated.username, password: '' })
      setSettings((previous) => previous && { ...previous, qbWebUiConfigured: updated.configured })
      setQbStatus(null); setMikanFeeds(null)
      if (updated.configured) void getQbStatus().then(setQbStatus).catch((cause) => setQbConfigError(errorText(cause)))
    } catch (cause) { setQbConfigError(errorText(cause)) }
    finally { setQbConfigBusy(false) }
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
      <Typography.Title level={4}>生效路径</Typography.Title>
      <Descriptions bordered column={1} items={[
        { key: 'source', label: '当前 qB 下载目录', children: settings.qbDownloadConfigured ? settings.sourcePath : '尚未配置 qB 下载目录' },
        { key: 'import', label: '外来导入目录', children: settings.importPath },
        { key: 'library', label: '媒体库目录', children: settings.libraryPath },
        { key: 'data', label: '数据目录', children: settings.dataPath },
      ]} />
      <label>qB 下载目录 <Input aria-label="qB 下载目录" value={qbPath} onChange={(event) => setQbPath(event.target.value)} maxLength={4096} placeholder="选择已有的 qB 下载文件夹路径" /></label>
      <Typography.Text type="secondary">仅识别你选择的 qB 下载目录；外来导入与媒体库保持独立。修改目录不会移动或删除已有文件。</Typography.Text>
      <Button loading={qbPathSaving} disabled={settings.qbDownloadEnvironmentOverride} onClick={() => void saveQbPath()}>保存 qB 下载目录</Button>
      {settings.qbDownloadEnvironmentOverride && <Alert type="info" message="环境变量正在覆盖页面保存的 qB 下载目录；移除 ANIME_VAULT_SOURCE_DIR 后才能在此修改。" />}
      {qbPathPending && <Alert type="warning" message="qB 下载目录已保存，重启后端后生效。" />}
      <Typography.Title level={4}>qB Web UI 连接</Typography.Title>
      <Typography.Text type="secondary">仅支持本机回环地址，例如 http://[::1]:8080。密码保存在当前 Windows 用户的凭据管理器中，不会回显；已保存配置时留空可保持原密码。</Typography.Text>
      {qbConfig?.source === 'saved' ? <Typography.Text>已保存 qB Web UI 配置</Typography.Text> : qbConfig?.source === 'environment' ? <Typography.Text>当前使用启动环境配置</Typography.Text> : <Typography.Text>尚未配置 qB Web UI</Typography.Text>}
      {qbConfigError && <Alert type="error" message={qbConfigError} />}
      {qbTestMessage && <Alert type={qbTestMessage.startsWith('测试成功') ? 'success' : 'warning'} message={qbTestMessage} />}
      <label>qB Web UI 地址 <Input aria-label="qB Web UI 地址" value={qbDraft.url} onChange={(event) => setQbDraft({ ...qbDraft, url: event.target.value })} maxLength={64} placeholder="http://[::1]:8080" /></label>
      <label>qB Web UI 用户名 <Input aria-label="qB Web UI 用户名" value={qbDraft.username} onChange={(event) => setQbDraft({ ...qbDraft, username: event.target.value })} maxLength={128} /></label>
      <label>qB Web UI 密码 <Input.Password aria-label="qB Web UI 密码" value={qbDraft.password} onChange={(event) => setQbDraft({ ...qbDraft, password: event.target.value })} maxLength={512} autoComplete="new-password" /></label>
      <Space>
        <Button loading={qbConfigBusy} disabled={!qbDraft.url || !qbDraft.username || (!qbDraft.password && qbConfig?.source !== 'saved')} onClick={() => void saveQbConnection()}>保存 qB 连接</Button>
        <Button loading={qbTestBusy} disabled={!qbDraft.url || !qbDraft.username || (!qbDraft.password && qbConfig?.source !== 'saved')} onClick={() => void testQbConnection()}>测试 qB 连接</Button>
        {qbConfig?.source === 'saved' && <Popconfirm title="确认清除已保存的 qB Web UI 配置？" description="清除后会恢复启动环境配置（若有），不会更改 qB 的下载任务。" okText="确认清除" cancelText="取消" onConfirm={() => void clearQbConnection()}><Button danger loading={qbConfigBusy}>清除 qB 配置</Button></Popconfirm>}
      </Space>
      <Alert type="info" message={settings.bangumiConfigured ? 'Bangumi 已配置（直接联网）' : 'Bangumi 未配置；本地浏览仍可用'} />
      <Alert type={qbStatus?.connected ? 'success' : 'info'} message={qbStatus?.connected ? `qBittorrent 已连接 · ${qbStatus.version} · ${qbStatus.torrentCount} 个任务（${qbStatus.completedCount} 个已下载）` : settings.qbWebUiConfigured ? `qBittorrent 已配置，${qbStatus ? `连接失败（${qbStatus.errorCode}）` : '正在检查连接'}` : 'qBittorrent Web UI 未启用；qB 下载完成需手动确认'} />
      <Typography.Title level={4}>Mikan 订阅（由 qB 获取 RSS）</Typography.Title>
      {mikanFeeds && <Typography.Text>{mikanFeeds.feedCount} 个订阅 · {mikanFeeds.articleCount} 篇文章 · {mikanFeeds.pairCount} 组标题映射</Typography.Text>}
      {mikanFeeds?.errorCode && <Alert type="warning" message={mikanFeeds.errorCode} />}
      {mikanError && <Alert type="warning" message={mikanError} />}
      <List size="small" dataSource={mikanFeeds?.feeds ?? []} locale={{ emptyText: '暂无可读取的 Mikan 订阅' }} renderItem={(feed) => <List.Item>{feed.title || 'Mikan 订阅'} · {feed.articleCount} 篇{feed.hasError ? ' · qB 获取失败' : ''}</List.Item>} />
      <Typography.Text type="secondary">新订阅首次获取完成后，已有条目将标记已读；随后自动建立每集下载首个可识别资源的 qB 规则，不限定画质或字幕。无法识别集数的条目会跳过。</Typography.Text>
      <Space.Compact><Input aria-label="Mikan RSS 地址" value={feedUrl} onChange={(event) => setFeedUrl(event.target.value)} maxLength={1024} placeholder="https://mikanani.me/RSS/Bangumi?..." /><Button loading={addingFeed} disabled={!settings.qbWebUiConfigured || !settings.qbDownloadConfigured || qbPathPending || !feedUrl.trim()} onClick={() => void addFeed()}>添加 Mikan 订阅</Button></Space.Compact>
      <Typography.Title level={5}>qB 自动下载规则</Typography.Title>
      <Typography.Text type="secondary">先将订阅加入 qB，再填写该订阅的完整地址。关键词按文章标题包含匹配；同名规则不会覆盖。下载目标固定为 {settings.sourcePath}。</Typography.Text>
      {ruleMessage && <Alert type={ruleMessage.startsWith('自动') ? 'success' : 'warning'} message={ruleMessage} />}
      <Input aria-label="规则名称" value={ruleName} onChange={(event) => setRuleName(event.target.value)} maxLength={80} placeholder="规则名称" />
      <Input aria-label="规则订阅地址" value={ruleFeedUrl} onChange={(event) => setRuleFeedUrl(event.target.value)} maxLength={1024} placeholder="https://mikanani.me/RSS/Bangumi?..." />
      <Input aria-label="标题包含关键词" value={ruleKeyword} onChange={(event) => setRuleKeyword(event.target.value)} maxLength={120} placeholder="例如字幕组名称或番剧名" />
      <Popconfirm title="确认创建自动下载规则？" description={`启用后 qB 可能立即下载已发布的匹配条目，保存到 ${settings.sourcePath}。`} okText="确认创建" cancelText="取消" onConfirm={() => void addRule()}>
        <Button loading={creatingRule} disabled={!settings.qbWebUiConfigured || !settings.qbDownloadConfigured || qbPathPending || !ruleName.trim() || !ruleFeedUrl.trim() || !ruleKeyword.trim()}>创建自动下载规则</Button>
      </Popconfirm>
      <Typography.Title level={4}>偏好</Typography.Title>
      <label>首选整理方式 <select aria-label="首选整理方式" value={draft.preferredOperation} onChange={(event) => setDraft({ ...draft, preferredOperation: event.target.value as Preferences['preferredOperation'] })}><option value="hardlink">硬链接</option><option value="copy">复制</option><option value="symlink">符号链接</option></select></label>
      <label>扫描间隔（预留，尚无自动扫描） <InputNumber aria-label="扫描间隔（秒）" min={60} max={86400} value={draft.scanIntervalSeconds} onChange={(value) => setDraft({ ...draft, scanIntervalSeconds: value ?? 3600 })} /></label>
        <label>mpv 可执行文件（本机播放） <Input aria-label="mpv 可执行文件" maxLength={1024} value={draft.mpvExecutable} onChange={(event) => setDraft({ ...draft, mpvExecutable: event.target.value })} /></label>
      <Button type="primary" loading={saving} onClick={() => void save()}>保存偏好</Button>
    </>}
    <Typography.Title level={4}>审计记录</Typography.Title>
    {auditError && <Alert type="error" message={auditError} />}
    {auditLoading ? <Spin aria-label="正在加载审计记录" /> : <List dataSource={audit} locale={{ emptyText: '暂无审计记录' }} renderItem={(item) => <List.Item>{item.createdAt} · {item.action} · {item.entityType} #{item.entityId}</List.Item>} />}
    <Space><Button aria-label="上一页审计" disabled={auditLoading || auditOffset === 0} onClick={() => setAuditOffset(Math.max(0, auditOffset - auditPageSize))}>上一页</Button><Button aria-label="下一页审计" disabled={auditLoading || nextAuditOffset === null} onClick={() => { if (nextAuditOffset !== null) setAuditOffset(nextAuditOffset) }}>下一页</Button></Space>
  </Space>
}
