# BrokerSimulator Manager UI

A React-based management interface for controlling BrokerSimulator backtest sessions, exploring APIs, and monitoring WebSocket streams.

## Features

- **Dashboard**: Overview of simulator services and active sessions
- **Session Management**: Create, start, pause, resume, and stop backtest sessions
- **API Explorer**: Test any API endpoint across Control, Alpaca, Polygon, and Finnhub simulators
- **WebSocket Monitor**: Connect to WebSocket streams, subscribe to channels, and view real-time messages
- **Settings**: Configure connection settings and appearance

## Tech Stack

- **React 18** + **TypeScript** + **Vite**
- **Zustand** for state management
- **TailwindCSS** + **Radix UI** for styling
- **Axios** for HTTP requests
- **Native WebSocket** for real-time data

## Getting Started

### Prerequisites

- Node.js 18+
- npm or yarn
- BrokerSimulator running on elanlinux

### Installation

```bash
cd manager-ui
npm install
```

### Development

```bash
# Start development server on port 5174
npm run dev

# Or use the startup script from project root
./start_ui.sh
```

### Production Build

```bash
npm run build
npm run preview

# Or use the startup script
./start_ui.sh build
```

## Configuration

Copy `.env.example` to `.env` and configure:

```bash
VITE_SIMULATOR_HOST=elanlinux
VITE_CONTROL_PORT=8000
VITE_ALPACA_PORT=8100
VITE_POLYGON_PORT=8200
VITE_FINNHUB_PORT=8300
VITE_WS_PORT=8400
```

Or configure in Settings page.

## Architecture

```
manager-ui/
├── src/
│   ├── api/           # API clients and endpoint definitions
│   ├── components/    # React components
│   │   ├── ui/        # Base UI components (Button, Card, etc.)
│   │   └── common/    # Shared components (Layout, JsonViewer)
│   ├── pages/         # Page components
│   ├── services/      # WebSocket service
│   ├── stores/        # Zustand stores
│   ├── types/         # TypeScript definitions
│   └── lib/           # Utility functions
├── public/            # Static assets
└── logs/              # Runtime logs
```

## Pages

### Dashboard (`/`)
- Service status indicators (Control, Alpaca, Polygon, Finnhub)
- Active session count and stats
- Recent sessions list
- Quick action buttons

### Sessions (`/sessions`)
- Create new backtest sessions with symbols, date range, capital
- Start/pause/resume/stop controls
- Speed factor adjustment (1x, 10x, 100x, max)
- Account state and performance metrics
- Position and order tracking

### API Explorer (`/explorer`)
- Predefined endpoints for all 4 services
- Custom request builder with method, path, params, body
- Response viewer with JSON formatting
- Request history

### WebSocket Monitor (`/websocket`)
- Connect to any WebSocket endpoint
- Authentication support (Alpaca/Polygon style)
- Subscribe to channels (trades, quotes, bars)
- Real-time message stream with filtering
- Export messages to JSON

### Settings (`/settings`)
- Connection configuration (host, ports)
- Theme toggle (dark/light)
- Quick links to simulator services

## Scripts

From project root:

```bash
# Start Manager UI
./start_ui.sh

# Start fresh (kill existing processes)
./start_ui.sh fresh

# Build and serve production
./start_ui.sh build

# Stop Manager UI
./stop_ui.sh
```

## API Integration

The UI connects to BrokerSimulator services running on elanlinux:

| Service | Port | Description |
|---------|------|-------------|
| Control | 8000 | Session management, time control |
| Alpaca  | 8100 | Trading API (orders, positions, account) |
| Polygon | 8200 | Market data (aggregates, trades, quotes) |
| Finnhub | 8300 | Fundamental data (news, earnings, etc.) |

## Development Notes

- Uses Vite proxy for API requests during development
- Zustand stores persist settings to localStorage
- WebSocket service is a singleton with connection management
- All components use TailwindCSS with CSS variables for theming
