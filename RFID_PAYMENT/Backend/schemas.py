from pydantic import BaseModel, EmailStr
from typing import Optional

class Account(BaseModel):
    id_card: str
    password: str
