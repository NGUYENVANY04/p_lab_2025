from sqlalchemy import Column,Float,Integer, String, Text, TIMESTAMP, func
from database import Base

class AccountModel(Base):
    __tablename__ = "account"

    id = Column(Integer, primary_key=True, index=True)
    id_card = Column(String(50), unique=True, index=True, nullable=False)
    id_student = Column(String(50), unique=True, index=True, nullable=True)
    password = Column(String(255), nullable=False)
    so_du = Column(Float, default=0.0)
    created_at = Column(TIMESTAMP, server_default=func.now())
    updated_at = Column(TIMESTAMP, server_default=func.now(), onupdate=func.now())