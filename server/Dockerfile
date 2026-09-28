FROM node:22-alpine

WORKDIR /app
COPY package.json server.js ./

# Stav sa ukladá do /data, ktorý je vo volume – nech naň smie písať `node`
RUN mkdir -p /data && chown -R node:node /data /app
USER node

ENV NODE_ENV=production \
    DATA_DIR=/data \
    PORT=8080

EXPOSE 8080

HEALTHCHECK --interval=30s --timeout=3s \
  CMD wget -qO- http://127.0.0.1:8080/healthz || exit 1

CMD ["node", "server.js"]
