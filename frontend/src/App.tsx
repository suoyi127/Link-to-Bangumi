import { lazy, Suspense, useState } from 'react'
import { ConfigProvider, Layout, Menu, Spin, Typography } from 'antd'
import './styles.css'

const DashboardPage = lazy(() => import('./components/DashboardPage').then(({ DashboardPage }) => ({ default: DashboardPage })))
const InboxPage = lazy(() => import('./components/InboxPage').then(({ InboxPage }) => ({ default: InboxPage })))
const LibraryPage = lazy(() => import('./components/LibraryPage').then(({ LibraryPage }) => ({ default: LibraryPage })))
const NovelLibraryPage = lazy(() => import('./components/NovelLibraryPage').then(({ NovelLibraryPage }) => ({ default: NovelLibraryPage })))
const GameLibraryPage = lazy(() => import('./components/GamePages').then(({ GameLibraryPage }) => ({ default: GameLibraryPage })))
const AnimeDetailPage = lazy(() => import('./components/AnimeDetailPage').then(({ AnimeDetailPage }) => ({ default: AnimeDetailPage })))
const SettingsPage = lazy(() => import('./components/SettingsPage').then(({ SettingsPage }) => ({ default: SettingsPage })))

const { Header, Sider, Content } = Layout
// 页面按需加载；番剧详情作为媒体库子视图由 animeId 控制。
const pages = [
  { key: 'dashboard', label: '仪表盘', description: '本地媒体与任务概览', glyph: '▦' },
  { key: 'inbox', label: '资源库', description: '扫描来源，识别与整理资源', glyph: '▤' },
  { key: 'library', label: '番剧库', description: '收藏、每周排期与本机播放', glyph: '▣' },
  { key: 'novels', label: '小说库', description: '作品、卷文件与本机阅读', glyph: '▥' },
  { key: 'games', label: '游戏库', description: '导入游戏、Bangumi 信息与本机启动', glyph: '◈' },
  { key: 'settings', label: '设置', description: '管理目录、连接与播放偏好', glyph: '⚙' },
]

export default function App() {
  const [page, setPage] = useState('dashboard')
  const [animeId, setAnimeId] = useState<number | null>(null)
  const title = pages.find((item) => item.key === page)?.label ?? '仪表盘'
  const navigate = (key: string) => { setAnimeId(null); setPage(key) }

  return (
    <ConfigProvider theme={{ token: {
      colorPrimary: '#287567', colorInfo: '#287567', colorBgLayout: '#f5f7f8', colorText: '#25333c',
      colorTextSecondary: '#697982', colorBorder: '#dfe6e9', colorBorderSecondary: '#e9eef0',
      borderRadius: 10, controlHeight: 38, fontSize: 14,
      fontFamily: 'Inter, "Segoe UI", "Microsoft YaHei", sans-serif',
    }, components: {
      Menu: { itemSelectedBg: '#eaf3f0', itemSelectedColor: '#23695e', itemHeight: 46, itemBorderRadius: 9 },
      Card: { headerFontSize: 15, paddingLG: 24 },
      Table: { headerBg: '#f8fafb', headerColor: '#60717b', cellPaddingBlock: 14 },
      Button: { primaryShadow: 'none', defaultShadow: 'none' },
    } }}>
      <Layout className="app-shell" style={{ minHeight: '100vh' }}>
        <Sider className="app-sidebar" width={216} theme="light" breakpoint="lg" collapsedWidth="0" trigger={null} style={{ position: 'sticky', top: 0, height: '100vh', overflowY: 'auto', alignSelf: 'flex-start' }}>
          <div className="app-brand"><span className="brand-mark" aria-hidden="true">AV</span><div><Typography.Title level={4}>Anime Vault</Typography.Title><span className="brand-caption">LINK TO BANGUMI</span></div></div>
          <div className="nav-caption">工作空间</div>
          <Menu mode="inline" selectedKeys={[page]} items={pages.map(({ key, label, glyph }) => ({ key, label, icon: <span className="nav-glyph" aria-hidden="true">{glyph}</span> }))} onClick={({ key }) => navigate(key)} />
          <div className="sidebar-note"><span className="status-dot" /> 本地媒体管理<br /><small>文件留在你的电脑中</small></div>
        </Sider>
        <Layout className="app-main">
          <Header className="app-header">
            <div><Typography.Title level={4}>{title}</Typography.Title><span>{pages.find((item) => item.key === page)?.description}</span></div>
            <span className="local-badge"><span className="status-dot" /> 本机工作区</span>
          </Header>
          <nav className="mobile-nav" aria-label="快捷导航">{pages.map((item) => <button key={item.key} aria-current={page === item.key ? 'page' : undefined} onClick={() => navigate(item.key)}>{item.label}</button>)}</nav>
          <Content className={`app-content page-${page}`} aria-label={`${title}内容`}>
            <div className="page-container">
            <Suspense fallback={<Spin aria-label="正在加载页面" />}>
              {page === 'dashboard' ? <DashboardPage /> : page === 'inbox' ? <InboxPage onOpenAnime={(id) => { setAnimeId(id); setPage('library') }} /> : page === 'library' ? animeId ? <AnimeDetailPage animeId={animeId} onBack={() => setAnimeId(null)} /> : <LibraryPage onOpenAnime={setAnimeId} /> : page === 'novels' ? <NovelLibraryPage /> : page === 'games' ? <GameLibraryPage /> : <SettingsPage />}
            </Suspense>
            </div>
          </Content>
        </Layout>
      </Layout>
    </ConfigProvider>
  )
}
