/**
 * Integration tests for Polygon Snapshots API
 * Tests: all tickers, single ticker, gainers/losers
 */

import { polygonClient, simulatorClient, config, logTestResult } from './setup';
import { extractSchema, compareSchemas, formatComparisonResult } from '../utils/schema-compare';

describe('Polygon Snapshots API', () => {
  describe('GET /v2/snapshot/locale/us/markets/stocks/tickers', () => {
    describe('Required Parameters', () => {
      it('should return matching schema for all tickers snapshot', async () => {
        const polygonResponse = await polygonClient.getAllTickersSnapshot();
        const simulatorResponse = await simulatorClient.getAllTickersSnapshot();

        const polygonSchema = extractSchema(polygonResponse.data);
        const simulatorSchema = extractSchema(simulatorResponse.data);
        const comparison = compareSchemas(polygonSchema, simulatorSchema);

        logTestResult(
          'All tickers snapshot',
          polygonResponse.status,
          simulatorResponse.status,
          comparison.match
        );

        if (!comparison.match) {
          console.log(formatComparisonResult(comparison));
        }

        expect(polygonResponse.status).toBe(simulatorResponse.status);
        expect(comparison.match).toBe(true);
      });
    });

    describe('Optional Parameters', () => {
      it('should handle tickers filter parameter', async () => {
        const tickers = config.testSymbols.join(',');

        const polygonResponse = await polygonClient.getAllTickersSnapshot({ tickers });
        const simulatorResponse = await simulatorClient.getAllTickersSnapshot({ tickers });

        const polygonSchema = extractSchema(polygonResponse.data);
        const simulatorSchema = extractSchema(simulatorResponse.data);
        const comparison = compareSchemas(polygonSchema, simulatorSchema);

        logTestResult(
          'Snapshot with tickers filter',
          polygonResponse.status,
          simulatorResponse.status,
          comparison.match
        );

        expect(polygonResponse.status).toBe(simulatorResponse.status);
        expect(comparison.match).toBe(true);
      });

      it('should handle include_otc=false parameter', async () => {
        const polygonResponse = await polygonClient.getAllTickersSnapshot({ include_otc: false });
        const simulatorResponse = await simulatorClient.getAllTickersSnapshot({ include_otc: false });

        const polygonSchema = extractSchema(polygonResponse.data);
        const simulatorSchema = extractSchema(simulatorResponse.data);
        const comparison = compareSchemas(polygonSchema, simulatorSchema);

        logTestResult(
          'Snapshot include_otc=false',
          polygonResponse.status,
          simulatorResponse.status,
          comparison.match
        );

        expect(polygonResponse.status).toBe(simulatorResponse.status);
        expect(comparison.match).toBe(true);
      });
    });
  });

  describe('GET /v2/snapshot/locale/us/markets/stocks/tickers/{symbol}', () => {
    describe('Required Parameters', () => {
      it.each(config.testSymbols)(
        'should return matching schema for %s ticker snapshot',
        async (symbol) => {
          const polygonResponse = await polygonClient.getTickerSnapshot(symbol);
          const simulatorResponse = await simulatorClient.getTickerSnapshot(symbol);

          const polygonSchema = extractSchema(polygonResponse.data);
          const simulatorSchema = extractSchema(simulatorResponse.data);
          const comparison = compareSchemas(polygonSchema, simulatorSchema);

          logTestResult(
            `Ticker snapshot ${symbol}`,
            polygonResponse.status,
            simulatorResponse.status,
            comparison.match
          );

          if (!comparison.match) {
            console.log(formatComparisonResult(comparison));
          }

          expect(polygonResponse.status).toBe(simulatorResponse.status);
          expect(comparison.match).toBe(true);
        }
      );
    });
  });

  describe('GET /v2/snapshot/locale/us/markets/stocks/{direction}', () => {
    describe('Required Parameters', () => {
      it('should return matching schema for gainers', async () => {
        const polygonResponse = await polygonClient.getGainersLosers('gainers');
        const simulatorResponse = await simulatorClient.getGainersLosers('gainers');

        const polygonSchema = extractSchema(polygonResponse.data);
        const simulatorSchema = extractSchema(simulatorResponse.data);
        const comparison = compareSchemas(polygonSchema, simulatorSchema);

        logTestResult(
          'Gainers snapshot',
          polygonResponse.status,
          simulatorResponse.status,
          comparison.match
        );

        if (!comparison.match) {
          console.log(formatComparisonResult(comparison));
        }

        expect(polygonResponse.status).toBe(simulatorResponse.status);
        expect(comparison.match).toBe(true);
      });

      it('should return matching schema for losers', async () => {
        const polygonResponse = await polygonClient.getGainersLosers('losers');
        const simulatorResponse = await simulatorClient.getGainersLosers('losers');

        const polygonSchema = extractSchema(polygonResponse.data);
        const simulatorSchema = extractSchema(simulatorResponse.data);
        const comparison = compareSchemas(polygonSchema, simulatorSchema);

        logTestResult(
          'Losers snapshot',
          polygonResponse.status,
          simulatorResponse.status,
          comparison.match
        );

        if (!comparison.match) {
          console.log(formatComparisonResult(comparison));
        }

        expect(polygonResponse.status).toBe(simulatorResponse.status);
        expect(comparison.match).toBe(true);
      });
    });
  });
});
