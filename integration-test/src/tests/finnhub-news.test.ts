/**
 * Integration tests for Finnhub news endpoints
 * Tests: /company-news, /news, /news-sentiment
 */

import { finnhubClient, finnhubSimulatorClient, config, logTestResult, finnhubApiAvailable } from './setup';
import { extractSchema, compareSchemas, formatComparisonResult } from '../utils/schema-compare';

const describeIfKey = config.finnhubApiKey ? describe : describe.skip;

function skipIfUnavailable(): boolean {
  if (!finnhubApiAvailable) {
    console.warn('Skipping Finnhub tests: FINNHUB_API_KEY invalid.');
    return true;
  }
  return false;
}

describeIfKey('Finnhub News API', () => {
  const from = config.finnhubTestStartDate;
  const to = config.finnhubTestEndDate;

  describe('GET /company-news', () => {
    it.each(config.testSymbols)('should return matching schema for %s company news', async (symbol) => {
      if (skipIfUnavailable()) {
        return;
      }
      const finnhubResponse = await finnhubClient.getCompanyNews(symbol, from, to);
      const simulatorResponse = await finnhubSimulatorClient.getCompanyNews(symbol, from, to);

      const finnhubSchema = extractSchema(finnhubResponse.data);
      const simulatorSchema = extractSchema(simulatorResponse.data);
      const comparison = compareSchemas(finnhubSchema, simulatorSchema);

      logTestResult(
        `Finnhub company news ${symbol}`,
        finnhubResponse.status,
        simulatorResponse.status,
        comparison.match
      );

      if (!comparison.match) {
        console.log(formatComparisonResult(comparison));
      }

      expect(finnhubResponse.status).toBe(simulatorResponse.status);
      expect(comparison.match).toBe(true);
    });
  });

  describe('GET /news', () => {
    it('should return matching schema for market news', async () => {
      if (skipIfUnavailable()) {
        return;
      }
      const finnhubResponse = await finnhubClient.getMarketNews('general');
      const simulatorResponse = await finnhubSimulatorClient.getMarketNews('general');

      const finnhubSchema = extractSchema(finnhubResponse.data);
      const simulatorSchema = extractSchema(simulatorResponse.data);
      const comparison = compareSchemas(finnhubSchema, simulatorSchema);

      logTestResult(
        'Finnhub market news',
        finnhubResponse.status,
        simulatorResponse.status,
        comparison.match
      );

      if (!comparison.match) {
        console.log(formatComparisonResult(comparison));
      }

      expect(finnhubResponse.status).toBe(simulatorResponse.status);
      expect(comparison.match).toBe(true);
    });
  });

  describe('GET /news-sentiment', () => {
    it.each(config.testSymbols)('should return matching schema for %s news sentiment', async (symbol) => {
      if (skipIfUnavailable()) {
        return;
      }
      const finnhubResponse = await finnhubClient.getNewsSentiment(symbol);
      const simulatorResponse = await finnhubSimulatorClient.getNewsSentiment(symbol);

      const finnhubSchema = extractSchema(finnhubResponse.data);
      const simulatorSchema = extractSchema(simulatorResponse.data);
      const comparison = compareSchemas(finnhubSchema, simulatorSchema);

      logTestResult(
        `Finnhub news sentiment ${symbol}`,
        finnhubResponse.status,
        simulatorResponse.status,
        comparison.match
      );

      if (!comparison.match) {
        console.log(formatComparisonResult(comparison));
      }

      expect(finnhubResponse.status).toBe(simulatorResponse.status);
      expect(comparison.match).toBe(true);
    });
  });
});
