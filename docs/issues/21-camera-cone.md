---
title: Replace the guessed camera cone with the real camera
labels: research, arena
---

Spawn logic avoids the camera's view with a cone of about 78 degrees from the hero heading. The camera object's address was never found directly (`camscan` finds float3 candidates by memory scanning).
Find the camera (FOV, position, forward vector) and use it for visibility checks, spawn steering and the "bodies in view" culling rule.
