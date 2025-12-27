/**
 * Integration tests for Polygon Market Status API
 * Tests: /v1/marketstatus/now, /v1/marketstatus/upcoming
 */

import { polygonClient, simulatorClient, logTestResult } from './setup';
import { extractSchema, compareSchemas, formatComparisonResult } from '../utils/schema-compare';
import { retryRequest } from '../utils/retry';

describe('Polygon Market Status API', () => {
  describe('GET /v1/marketstatus/now', () => {
    describe('Required Parameters', () => {
      it('should return matching schema for current market status', async () => {
        const polygonResponse = await retryRequest(() => polygonClient.getMarketStatus());
        const simulatorResponse = await simulatorClient.getMarketStatus();

        const polygonSchema = extractSchema(polygonResponse.data);
        const simulatorSchema = extractSchema(simulatorResponse.data);
        const comparison = compareSchemas(polygonSchema, simulatorSchema);

        logTestResult(
          'Market status now',
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

  describe('GET /v1/marketstatus/upcoming', () => {
    describe('Required Parameters', () => {
      it('should return matching schema for upcoming market holidays', async () => {
        const polygonResponse = await retryRequest(() => polygonClient.getMarketHolidays());
        const simulatorResponse = await simulatorClient.getMarketHolidays();

        const polygonSchema = extractSchema(polygonResponse.data);
        const simulatorSchema = extractSchema(simulatorResponse.data);
        const comparison = compareSchemas(polygonSchema, simulatorSchema);

        logTestResult(
          'Market holidays upcoming',
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
