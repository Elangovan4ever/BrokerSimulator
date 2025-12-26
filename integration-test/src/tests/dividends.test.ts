/**
 * Integration tests for Polygon Dividends API
 * Tests: /v3/reference/dividends
 */

import { polygonClient, simulatorClient, config, logTestResult } from './setup';
import { extractSchema, compareSchemas, formatComparisonResult } from '../utils/schema-compare';

describe('Polygon Dividends API', () => {
  describe('GET /v3/reference/dividends', () => {
    describe('Required Parameters', () => {
      it.each(config.testSymbols)(
        'should return matching schema for %s dividends',
        async (symbol) => {
          const polygonResponse = await polygonClient.getDividends({ ticker: symbol, limit: 10 });
          const simulatorResponse = await simulatorClient.getDividends({ ticker: symbol, limit: 10 });

          const polygonSchema = extractSchema(polygonResponse.data);
          const simulatorSchema = extractSchema(simulatorResponse.data);
          const comparison = compareSchemas(polygonSchema, simulatorSchema);

          logTestResult(
            `Dividends ${symbol}`,
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

    describe('Optional Parameters', () => {
      it('should handle ex_dividend_date.gte parameter', async () => {
        const symbol = config.testSymbols[0];

        const polygonResponse = await polygonClient.getDividends({
          ticker: symbol,
          'ex_dividend_date.gte': '2024-01-01',
          limit: 10,
        });
        const simulatorResponse = await simulatorClient.getDividends({
          ticker: symbol,
          'ex_dividend_date.gte': '2024-01-01',
          limit: 10,
        });

        const polygonSchema = extractSchema(polygonResponse.data);
        const simulatorSchema = extractSchema(simulatorResponse.data);
        const comparison = compareSchemas(polygonSchema, simulatorSchema);

        logTestResult(
          'Dividends ex_dividend_date.gte',
          polygonResponse.status,
          simulatorResponse.status,
          comparison.match
        );

        expect(polygonResponse.status).toBe(simulatorResponse.status);
        expect(comparison.match).toBe(true);
      });

      it('should handle ex_dividend_date.lte parameter', async () => {
        const symbol = config.testSymbols[0];

        const polygonResponse = await polygonClient.getDividends({
          ticker: symbol,
          'ex_dividend_date.lte': '2025-12-31',
          limit: 10,
        });
        const simulatorResponse = await simulatorClient.getDividends({
          ticker: symbol,
          'ex_dividend_date.lte': '2025-12-31',
          limit: 10,
        });

        const polygonSchema = extractSchema(polygonResponse.data);
        const simulatorSchema = extractSchema(simulatorResponse.data);
        const comparison = compareSchemas(polygonSchema, simulatorSchema);

        logTestResult(
          'Dividends ex_dividend_date.lte',
          polygonResponse.status,
          simulatorResponse.status,
          comparison.match
        );

        expect(polygonResponse.status).toBe(simulatorResponse.status);
        expect(comparison.match).toBe(true);
      });

      it('should handle order=asc parameter', async () => {
        const symbol = config.testSymbols[0];

        const polygonResponse = await polygonClient.getDividends({
          ticker: symbol,
          order: 'asc',
          limit: 10,
        });
        const simulatorResponse = await simulatorClient.getDividends({
          ticker: symbol,
          order: 'asc',
          limit: 10,
        });

        const polygonSchema = extractSchema(polygonResponse.data);
        const simulatorSchema = extractSchema(simulatorResponse.data);
        const comparison = compareSchemas(polygonSchema, simulatorSchema);

        logTestResult(
          'Dividends order=asc',
          polygonResponse.status,
          simulatorResponse.status,
          comparison.match
        );

        expect(polygonResponse.status).toBe(simulatorResponse.status);
        expect(comparison.match).toBe(true);
      });

      it('should handle order=desc parameter', async () => {
        const symbol = config.testSymbols[0];

        const polygonResponse = await polygonClient.getDividends({
          ticker: symbol,
          order: 'desc',
          limit: 10,
        });
        const simulatorResponse = await simulatorClient.getDividends({
          ticker: symbol,
          order: 'desc',
          limit: 10,
        });

        const polygonSchema = extractSchema(polygonResponse.data);
        const simulatorSchema = extractSchema(simulatorResponse.data);
        const comparison = compareSchemas(polygonSchema, simulatorSchema);

        logTestResult(
          'Dividends order=desc',
          polygonResponse.status,
          simulatorResponse.status,
          comparison.match
        );

        expect(polygonResponse.status).toBe(simulatorResponse.status);
        expect(comparison.match).toBe(true);
      });

      it('should handle limit parameter', async () => {
        const symbol = config.testSymbols[0];

        const polygonResponse = await polygonClient.getDividends({
          ticker: symbol,
          limit: 5,
        });
        const simulatorResponse = await simulatorClient.getDividends({
          ticker: symbol,
          limit: 5,
        });

        const polygonSchema = extractSchema(polygonResponse.data);
        const simulatorSchema = extractSchema(simulatorResponse.data);
        const comparison = compareSchemas(polygonSchema, simulatorSchema);

        logTestResult(
          'Dividends limit=5',
          polygonResponse.status,
          simulatorResponse.status,
          comparison.match
        );

        expect(polygonResponse.status).toBe(simulatorResponse.status);
        expect(comparison.match).toBe(true);
      });

      it('should handle dividend_type parameter', async () => {
        const symbol = config.testSymbols[0];

        const polygonResponse = await polygonClient.getDividends({
          ticker: symbol,
          dividend_type: 'CD',
          limit: 10,
        });
        const simulatorResponse = await simulatorClient.getDividends({
          ticker: symbol,
          dividend_type: 'CD',
          limit: 10,
        });

        const polygonSchema = extractSchema(polygonResponse.data);
        const simulatorSchema = extractSchema(simulatorResponse.data);
        const comparison = compareSchemas(polygonSchema, simulatorSchema);

        logTestResult(
          'Dividends dividend_type=CD',
          polygonResponse.status,
          simulatorResponse.status,
          comparison.match
        );

        expect(polygonResponse.status).toBe(simulatorResponse.status);
        expect(comparison.match).toBe(true);
      });
    });
  });
});
