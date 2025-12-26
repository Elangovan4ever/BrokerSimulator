/**
 * Integration tests for Polygon Daily Open/Close API
 * Tests: /v1/open-close/{symbol}/{date}
 */

import { polygonClient, simulatorClient, config, logTestResult } from './setup';
import { extractSchema, compareSchemas, formatComparisonResult } from '../utils/schema-compare';

describe('Polygon Open/Close API', () => {
  const testDate = config.testStartDate;

  describe('GET /v1/open-close/{symbol}/{date}', () => {
    describe('Required Parameters', () => {
      it.each(config.testSymbols)(
        'should return matching schema for %s daily open/close',
        async (symbol) => {
          const polygonResponse = await polygonClient.getDailyOpenClose(symbol, testDate);
          const simulatorResponse = await simulatorClient.getDailyOpenClose(symbol, testDate);

          const polygonSchema = extractSchema(polygonResponse.data);
          const simulatorSchema = extractSchema(simulatorResponse.data);
          const comparison = compareSchemas(polygonSchema, simulatorSchema);

          logTestResult(
            `Open/Close ${symbol}`,
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
      it('should handle adjusted=true parameter', async () => {
        const symbol = config.testSymbols[0];

        const polygonResponse = await polygonClient.getDailyOpenClose(symbol, testDate, {
          adjusted: true,
        });
        const simulatorResponse = await simulatorClient.getDailyOpenClose(symbol, testDate, {
          adjusted: true,
        });

        const polygonSchema = extractSchema(polygonResponse.data);
        const simulatorSchema = extractSchema(simulatorResponse.data);
        const comparison = compareSchemas(polygonSchema, simulatorSchema);

        logTestResult(
          'Open/Close adjusted=true',
          polygonResponse.status,
          simulatorResponse.status,
          comparison.match
        );

        expect(polygonResponse.status).toBe(simulatorResponse.status);
        expect(comparison.match).toBe(true);
      });

      it('should handle adjusted=false parameter', async () => {
        const symbol = config.testSymbols[0];

        const polygonResponse = await polygonClient.getDailyOpenClose(symbol, testDate, {
          adjusted: false,
        });
        const simulatorResponse = await simulatorClient.getDailyOpenClose(symbol, testDate, {
          adjusted: false,
        });

        const polygonSchema = extractSchema(polygonResponse.data);
        const simulatorSchema = extractSchema(simulatorResponse.data);
        const comparison = compareSchemas(polygonSchema, simulatorSchema);

        logTestResult(
          'Open/Close adjusted=false',
          polygonResponse.status,
          simulatorResponse.status,
          comparison.match
        );

        expect(polygonResponse.status).toBe(simulatorResponse.status);
        expect(comparison.match).toBe(true);
      });
    });

    describe('Multiple Dates', () => {
      it('should return matching schema for different trading days', async () => {
        const symbol = config.testSymbols[0];
        const dates = ['2025-01-13', '2025-01-14', '2025-01-15'];

        for (const date of dates) {
          const polygonResponse = await polygonClient.getDailyOpenClose(symbol, date);
          const simulatorResponse = await simulatorClient.getDailyOpenClose(symbol, date);

          const polygonSchema = extractSchema(polygonResponse.data);
          const simulatorSchema = extractSchema(simulatorResponse.data);
          const comparison = compareSchemas(polygonSchema, simulatorSchema);

          logTestResult(
            `Open/Close ${symbol} ${date}`,
            polygonResponse.status,
            simulatorResponse.status,
            comparison.match
          );

          expect(polygonResponse.status).toBe(simulatorResponse.status);
          expect(comparison.match).toBe(true);
        }
      });
    });
  });
});
