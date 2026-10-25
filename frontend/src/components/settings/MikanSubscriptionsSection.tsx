import { Alert, Button, Input, List, Popconfirm, Space, Typography } from 'antd'
import { SettingsSection } from './SettingsSection'
import type { useMikanSubscriptions } from './useMikanSubscriptions'
import type { Settings } from '../../api/types'

export function MikanSubscriptionsSection({ settings, qbPathPending, controller }: { settings: Settings; qbPathPending: boolean; controller: ReturnType<typeof useMikanSubscriptions> }) {
  const { qbStatus, mikanFeeds, mikanError, feedUrl, setFeedUrl, addingFeed, ruleName, setRuleName, ruleFeedUrl, setRuleFeedUrl, ruleKeyword, setRuleKeyword, creatingRule, ruleMessage, addFeed, addRule } = controller
  return <SettingsSection title="Mikan 订阅（由 qB 获取 RSS）" wide>
      <Alert type={qbStatus?.connected ? 'success' : 'info'} message={qbStatus?.connected ? `qBittorrent 已连接 · ${qbStatus.version} · ${qbStatus.torrentCount} 个任务（${qbStatus.completedCount} 个已下载）` : settings.qbWebUiConfigured ? `qBittorrent 已配置，${qbStatus ? `连接失败（${qbStatus.errorCode}）` : '正在检查连接'}` : 'qBittorrent Web UI 未启用；qB 下载完成需手动确认'} />
      {mikanFeeds && <Typography.Text>{mikanFeeds.feedCount} 个订阅 · {mikanFeeds.articleCount} 篇文章 · {mikanFeeds.pairCount} 组标题映射</Typography.Text>}
      {mikanFeeds?.errorCode && <Alert type="warning" message={mikanFeeds.errorCode} />}
      {mikanError && <Alert type="warning" message={mikanError} />}
      <details className="mikan-feed-details"><summary>订阅列表与文章统计</summary>
      <List size="small" dataSource={mikanFeeds?.feeds ?? []} locale={{ emptyText: '暂无可读取的 Mikan 订阅' }} renderItem={(feed) => <List.Item>{feed.title || 'Mikan 订阅'} · {feed.articleCount} 篇{feed.hasError ? ' · qB 获取失败' : ''}</List.Item>} />
      </details>
      <Typography.Text type="secondary">新订阅获取完成后自动建立正则及智能集数过滤规则，每集仅下载首个符合条件的版本，不限定字幕组、画质或平台。立即下载，忽略间隔为 0 天，不自动下载 REPACK；已有任务对应的集数会补入去重记录。每个源仅用于一个季度及一种集数编号。</Typography.Text>
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
      </SettingsSection>
}
